/*
 * Read two sensors, send the numbers, sleep. Repeat until the batteries outlast us.
 *
 * The whole job is in `loop()` below and it is deliberately boring. Anything interesting
 * lives in one of the modules, so that when something misbehaves in the field there is
 * exactly one place to look for each kind of problem.
 *
 * Subsystems are switched on by the build environment (see build_features.h), so the same
 * source can be flashed as wind-sensor-only, then with the battery, then with the radio,
 * then with sleep. Each build adds one new way to fail.
 *
 * Behavior contract:  docs/FIRMWARE_SPEC.md
 * Wiring:             docs/HARDWARE.md · docs/decisions/ADR-0004-bms-one-wire-path.md
 * Payload contract:   payload/schema.yaml
 */

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

#include "config.h"
#include "build_features.h"
#include "payload.h"
#include "power.h"
#include "radio.h"
#include "reading.h"
#include "sensors/battery.h"
#include "sensors/rk900.h"

#if FEATURE_BUS_SCAN
#include "sensors/crc16.h"
#endif

namespace {

// CITE(datasheet): [CIT-RAK19007] the base board's IO slot exposes WB_IO1..WB_IO6; the
//   one-wire battery link lands on WB_IO1, leaving Serial1 free for the RS-485 module.
// CITE(datasheet): [CIT-RAK5802] the RS-485 module occupies the IO slot and its
//   transceiver is powered from the switched rail on WB_IO2, which rk900.cpp controls.
constexpr uint8_t kBatteryPin = WB_IO1;

// Ceiling on one awake cycle. Two sensor reads with bounded retries plus a join attempt
// and the receive windows fit inside this with room to spare; anything longer means
// something is stuck and a reset is the correct answer. The counter pauses during sleep,
// so this measures awake time only.
constexpr uint32_t kWatchdogSeconds = 120;

// How long to wait for the USB console at boot. A deployed node has no host attached, so
// waiting forever would make a perfectly healthy board look dead.
constexpr uint32_t kConsoleWaitMs = 3000;

// Ceiling on the between-cycle wait when sleep is compiled out, so bring-up is not spent
// watching a blank screen for an hour. On a bench build the ceiling has to be at least the
// bench interval or it would silently override the cadence the operator asked for — a 30 s
// cap against a 60 s interval reads as the setting having been ignored.
#if FEATURE_BENCH_INTERVAL
constexpr uint32_t kAwakeWaitCapSeconds = kIntervalDefaultSeconds;
#else
constexpr uint32_t kAwakeWaitCapSeconds = 30;
#endif

Config           config;
Radio            radio;
RK900            weather_sensor;
Battery          battery_sensor(kBatteryPin);
power::Brownout  brownout;

uint32_t cycle = 0;

// Consecutive cycles in which neither sensor produced a single field.
uint32_t empty_cycles = 0;

// How often to transmit proof of life while both sensors stay silent. The first quiet cycle
// reports immediately, so a wiring mistake made during installation is visible before anyone
// leaves the site; after that the rate drops to keep a permanently broken sensor from
// spending the airtime budget on saying nothing. At the shortest permitted interval this is
// roughly one uplink every four hours.
#if FEATURE_RADIO
constexpr uint32_t kQuietCyclesPerHeartbeat = 8;

bool heartbeat_due(uint32_t quiet_cycles)
{
    return quiet_cycles == 1 || (quiet_cycles % kQuietCyclesPerHeartbeat) == 0;
}
#endif

#if FEATURE_BUS_SCAN
// The driver collapses every unproductive outcome into "timeout", which cannot tell a
// sensor that said nothing at all apart from one that answered in a framing this build
// cannot read. That distinction decides whether the next move is a code change or a trip to
// the bench, so this reports the raw bytes and lets the reader judge.
//
// CITE(datasheet): [CIT-RK900] the sensor is fixed at 4800 8N1, slave 0x01. The other
//   combinations are swept only to establish that the line is silent everywhere, not
//   because any of them is expected to answer.
// CITE(sibling): forest-weather-machines LoRaWAN/docs/RAK2560_weather_station_settings.md @ efc0e3c
//   — the deployed Sensor Hub reads this same sensor at 4800 8N1, slave 01, FC 0x03,
//   holding registers 0x0000-0x0004. The constants under test are field-proven, so a
//   silent line is not a wrong constant.
// CITE(spec): [CIT-MODBUS-APP] FC 0x03 request framing, address first, CRC low byte first.
constexpr uint32_t kScanBauds[]  = {4800, 9600, 19200, 38400, 115200};
constexpr uint8_t  kScanSlaves[] = {0x01, 0x02, 0x03, 0x6E};

// Long enough for a five-register reply to finish at the slowest rate swept, short enough
// that the whole sweep stays well inside the watchdog window.
constexpr uint32_t kScanListenMs = 400;

uint32_t scan_one(uint32_t baud, uint8_t slave)
{
    uint8_t req[8];
    req[0] = slave;
    req[1] = 0x03;
    req[2] = 0x00;
    req[3] = 0x00;
    req[4] = 0x00;
    req[5] = 0x01;

    const uint16_t crc = modbus_crc16(req, 6);
    req[6] = (uint8_t)(crc & 0xFF);
    req[7] = (uint8_t)(crc >> 8);

    while (Serial1.available()) {
        (void)Serial1.read();
    }
    Serial1.write(req, sizeof(req));
    Serial1.flush();

    // Every byte is kept, valid or not. A malformed reply is the most informative result
    // this scan can produce — it proves the sensor is powered and the pair is the right way
    // round, leaving only the framing to fix.
    uint8_t        got[64];
    uint32_t       n     = 0;
    const uint32_t start = millis();
    while ((millis() - start) < kScanListenMs && n < sizeof(got)) {
        if (Serial1.available()) {
            got[n++] = (uint8_t)Serial1.read();
        }
    }

    LOGF("   %6lu baud  slave 0x%02X : %lu byte(s)", (unsigned long)baud, slave,
         (unsigned long)n);
    if (n > 0) {
        LOG("  <-");
        for (uint32_t i = 0; i < n; i++) {
            LOGF(" %02X", got[i]);
        }
    }
    LOGLN();
    return n;
}

uint32_t sweep(bool rail_on)
{
    // CITE(datasheet): [CIT-RAK5802] the transceiver runs from the switched 3V3_S rail,
    //   gated by WB_IO2. The sweep is run twice, once with the rail up and once with it
    //   down, because that comparison is the only way to tell a real reply from a floating
    //   receiver. Bytes that appear identically with the transceiver unpowered came from
    //   nothing but an undriven input, and mean the opposite of what they look like.
    pinMode(WB_IO2, OUTPUT);
    digitalWrite(WB_IO2, rail_on ? HIGH : LOW);
    delay(50);

    LOGF("[bus scan] WB_IO2 %s, A/B as wired, FC 0x03 read 0x0000 x1\n",
         rail_on ? "HIGH (transceiver powered)" : "LOW (transceiver unpowered)");

    uint32_t total = 0;
    for (uint32_t b = 0; b < (sizeof(kScanBauds) / sizeof(kScanBauds[0])); b++) {
        Serial1.begin(kScanBauds[b]);
        delay(20);
        for (uint32_t s = 0; s < (sizeof(kScanSlaves) / sizeof(kScanSlaves[0])); s++) {
            total += scan_one(kScanBauds[b], kScanSlaves[s]);
            power::watchdog_feed();
        }
        Serial1.end();
    }

    LOGF("[bus scan] total with rail %s: %lu byte(s)\n", rail_on ? "HIGH" : "LOW",
         (unsigned long)total);
    return total;
}

void bus_scan()
{
    const uint32_t powered   = sweep(true);
    const uint32_t unpowered = sweep(false);

    // Restored so the pin is not left driving the rail down between cycles.
    digitalWrite(WB_IO2, HIGH);

    LOGF("[bus scan] verdict: %lu byte(s) powered vs %lu unpowered\n",
         (unsigned long)powered, (unsigned long)unpowered);

    if (powered == 0 && unpowered == 0) {
        LOGLN(F("[bus scan] the line is dead in both states. Nothing the firmware controls"));
        LOGLN(F("           can change that — check 12 V at the RK900 and the A/B pair."));
    } else if (powered == unpowered) {
        LOGLN(F("[bus scan] identical with the transceiver unpowered, so those bytes are a"));
        LOGLN(F("           floating receiver, not a reply. Nothing is answering on the bus."));
    } else {
        LOGLN(F("[bus scan] the counts differ, so something really is driving the pair."));
        LOGLN(F("           Read the hex above before changing any constant."));
    }
    LOGLN();
}
#endif

void print_banner()
{
    LOGLN();
    LOGLN(F("=== rak-sensor-node ==="));
    LOGF("firmware : %s\n", FIRMWARE_VERSION);
    LOGF("built    : %s %s\n", __DATE__, __TIME__);
    LOGF("features : rk900=%d battery=%d radio=%d sleep=%d wdt=%d\n", FEATURE_RK900,
         FEATURE_BATTERY, FEATURE_RADIO, FEATURE_SLEEP, FEATURE_WATCHDOG);
    // Compile-time bounds, not the live value — config.begin() prints that a moment later,
    // and the two together say whether a stored setting is in play. Printed because a build
    // running the bench cadence must be identifiable from the console alone; nothing else
    // distinguishes it from the field image at a glance.
    LOGF("interval : bench=%d, bounds %lu-%lu s, default %lu s\n", FEATURE_BENCH_INTERVAL,
         (unsigned long)kIntervalMinSeconds, (unsigned long)kIntervalMaxSeconds,
         (unsigned long)kIntervalDefaultSeconds);

#if FEATURE_RADIO
    // Printed so the identity the node is actually joining with can be compared against what
    // the network server has registered, without reading it back out of the binary. A
    // byte-order mistake in these is invisible from the network side — the join request simply
    // never matches a known device, and the network stays silent rather than reporting a
    // rejection.
    LOGF("deveui   : %s\n", radio.deveui_hex());
    LOGF("appeui   : %s\n", radio.appeui_hex());
    LOGF("region   : US915 sub-band %u\n", radio.sub_band());
#endif

    if (power::reset_was_watchdog()) {
        // Worth shouting about. A node resetting every cycle still reports data and looks
        // healthy from the network side, so this is the only place it becomes visible.
        LOGLN(F("WARNING  : last reset came from the watchdog — something hung"));
    }
}

} // namespace

void setup()
{
    Serial.begin(115200);
    const uint32_t start = millis();
    while (!Serial && (millis() - start) < kConsoleWaitMs) {
        delay(10);
    }

#if FEATURE_WATCHDOG
    // Started before anything that can hang, so a failure during bring-up is recoverable
    // rather than permanent.
    power::watchdog_begin(kWatchdogSeconds);
#endif

    print_banner();
    config.begin();

#if FEATURE_RADIO
    radio.begin();
#endif

    LOGLN();
}

void loop()
{
    power::watchdog_feed();

    LOGF("[cycle %lu]\n", (unsigned long)++cycle);

#if FEATURE_BUS_SCAN
    // Nothing below this runs in a scan build. The point is to look at the line, not to
    // produce a reading or an uplink from it.
    bus_scan();
    delay(5000);
    return;
#endif

    WeatherReading weather;
    BatteryReading pack;

    // Each sensor is read independently and neither can prevent the other from being
    // read. A sensor that fails contributes no fields rather than zeroes, so a gap in the
    // data is visible as a gap instead of arriving as a plausible wrong number.
#if FEATURE_RK900
    weather = weather_sensor.read();
    power::watchdog_feed();
#endif

#if FEATURE_BATTERY
    pack = battery_sensor.read();
    power::watchdog_feed();

    // Spec H3. Below the threshold the node keeps waking and reading but stops spending
    // energy on transmission, because a transmit burst is the largest current it ever
    // draws and the pack's protection circuit disconnecting is the one failure that
    // requires somebody to walk in.
    brownout.update(pack.voltage.valid, pack.voltage.value);
#endif

    // Built to fit what the current data rate allows. The network decides that rate, and
    // at its slowest only 11 of the 35 bytes fit — so the encoder fills the space in
    // priority order rather than producing an uplink that would simply be refused.
    Payload payload;
#if FEATURE_RADIO
    payload.build(weather, pack, radio.max_payload());
    if (payload.dropped() > 0) {
        LOGF("   uplink  : %u field(s) dropped to fit %u bytes\n", payload.dropped(),
             (unsigned)radio.max_payload());
    }
#else
    payload.build(weather, pack);
#endif

    if (!payload.empty()) {
        empty_cycles = 0;
    }

    uint32_t sleep_for = config.interval_seconds();

#if FEATURE_RADIO
    if (!brownout.transmit_allowed()) {
        // Deliberately still reading the sensors and still waking on schedule. The pack
        // recovers on sunlight, not on being left alone, and the node has to notice the
        // moment it can transmit again.
        LOGLN(F("   uplink  : held — pack too low to transmit"));
    } else if (payload.empty() && !heartbeat_due(++empty_cycles)) {
        // Both sensors silent, and the last proof-of-life was recent enough. Reading
        // continues on schedule because the fault may clear on its own.
        LOGF("   uplink  : nothing to send (%lu quiet cycle(s))\n", (unsigned long)empty_cycles);
    } else if (radio.ensure_joined()) {
        if (payload.empty()) {
            // Deliberately transmitting with no measurements in it. Silence cannot be told
            // apart from a node that is gone, a flat pack, or a dead gateway, and all three
            // want different responses from whoever is deciding whether to drive out there.
            // An uplink carrying nothing still reports that the node is running, and — being
            // Class A, where a downlink can only follow an uplink — it is also the only thing
            // that reopens the path for commanding the node back to health remotely.
            LOGF("   uplink  : proof of life — no sensor data for %lu cycle(s)\n",
                 (unsigned long)empty_cycles);
        }
        if (radio.send(payload)) {
            DownlinkCommand cmd;
            if (radio.take_downlink(cmd)) {
                if (cmd.set_interval && brownout.flash_write_allowed()) {
                    config.set_interval_seconds(cmd.interval_value);
                }
                if (cmd.request_status) {
                    // The next cycle's uplink is the answer. Rather than transmitting
                    // twice, shorten the wait so it arrives promptly.
                    sleep_for = kIntervalMinSeconds;
                }
            }
        }
    }

    // Any failure — join or send — replaces the normal interval with a backoff that grows
    // and then holds. The node keeps trying forever at that ceiling, so a gateway that
    // returns after a week is picked up without anyone going out to restart anything.
    const uint32_t backoff = radio.backoff_seconds();
    if (backoff > 0) {
        sleep_for = backoff;
    }
#endif

    power::watchdog_feed();

#if FEATURE_SLEEP
    LOGF("   sleep   : %lu s\n\n", (unsigned long)sleep_for);
    power::sleep_seconds(sleep_for);
#else
    // Without sleep the node stays awake and simply waits, which keeps the console
    // attached and every cycle observable.
    const uint32_t awake_wait =
        (sleep_for > kAwakeWaitCapSeconds) ? kAwakeWaitCapSeconds : sleep_for;
    LOGF("   wait    : %lu s (sleep disabled)\n\n", (unsigned long)awake_wait);
    for (uint32_t i = 0; i < awake_wait; i++) {
        delay(1000);
        power::watchdog_feed();
    }
#endif
}

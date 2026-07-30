/*
 * Read two sensors, send the numbers, sleep. Repeat until the batteries outlast us.
 *
 * The whole job is in `loop()` below and it is deliberately boring. Anything interesting
 * lives in one of the modules, so that when something misbehaves in the field there is
 * exactly one place to look for each kind of problem.
 *
 * Subsystems are switched on by the build environment (see features.h), so the same
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
#include "features.h"
#include "payload.h"
#include "power.h"
#include "radio.h"
#include "reading.h"
#include "sensors/battery.h"
#include "sensors/rk900.h"

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

Config           config;
Radio            radio;
RK900            weather_sensor;
Battery          battery_sensor(kBatteryPin);
power::Brownout  brownout;

uint32_t cycle = 0;

void print_banner()
{
    LOGLN();
    LOGLN(F("=== rak-sensor-node ==="));
    LOGF("firmware : %s\n", FIRMWARE_VERSION);
    LOGF("built    : %s %s\n", __DATE__, __TIME__);
    LOGF("features : rk900=%d battery=%d radio=%d sleep=%d wdt=%d\n", FEATURE_RK900,
         FEATURE_BATTERY, FEATURE_RADIO, FEATURE_SLEEP, FEATURE_WATCHDOG);

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

    uint32_t sleep_for = config.interval_seconds();

#if FEATURE_RADIO
    if (!brownout.transmit_allowed()) {
        // Deliberately still reading the sensors and still waking on schedule. The pack
        // recovers on sunlight, not on being left alone, and the node has to notice the
        // moment it can transmit again.
        LOGLN(F("   uplink  : held — pack too low to transmit"));
    } else if (payload.empty()) {
        // Both sensors silent. Still worth waking on schedule — the fault may clear, and
        // an empty uplink would tell the network nothing it does not already infer from
        // the silence.
        LOGLN(F("   uplink  : nothing to send"));
    } else if (radio.ensure_joined()) {
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
    // attached and every cycle observable. Capped so bring-up is not spent watching a
    // blank screen for an hour.
    const uint32_t awake_wait = (sleep_for > 30) ? 30 : sleep_for;
    LOGF("   wait    : %lu s (sleep disabled)\n\n", (unsigned long)awake_wait);
    for (uint32_t i = 0; i < awake_wait; i++) {
        delay(1000);
        power::watchdog_feed();
    }
#endif
}

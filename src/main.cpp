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

#if FEATURE_ONEWIRE_SCAN
// The same byte layer the battery driver uses, driven directly here so the scan owns the
// line and the driver cannot be blamed for what it sees. FEATURE_BATTERY=0 in the owscan
// environment keeps the two from ever contending for the pin.
#include <SoftwareHalfSerial.h>
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
// CITE(sibling): forest-weather-machines (local sibling) — LoRaWAN/docs/RAK2560_weather_station_settings.md
//   — the deployed Sensor Hub reads this same sensor at 4800 8N1, slave 01, FC 0x03,
//   holding registers 0x0000-0x0004. The constants under test are field-proven, so a
//   silent line is not a wrong constant.
// CITE(spec): [CIT-MODBUS-APP] FC 0x03 request framing, address first, CRC low byte first.
constexpr uint32_t kScanBauds[]  = {4800, 9600, 19200, 38400, 115200};
constexpr uint8_t  kScanSlaves[] = {0x01, 0x02, 0x03, 0x6E};

// Long enough for a five-register reply to finish at the slowest rate swept, short enough
// that the whole sweep stays well inside the watchdog window.
constexpr uint32_t kScanListenMs = 400;

uint32_t scan_one(uint32_t baud, uint8_t slave, uint8_t register_count = 1)
{
    uint8_t req[8];
    req[0] = slave;
    req[1] = 0x03;
    req[2] = 0x00;
    req[3] = 0x00;
    req[4] = 0x00;
    req[5] = register_count;

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

    LOGF("   %6lu baud  slave 0x%02X  0x0000 x%u : %lu byte(s)",
         (unsigned long)baud, slave, register_count, (unsigned long)n);
    if (n > 0) {
        LOG("  <-");
        for (uint32_t i = 0; i < n; i++) {
            LOGF(" %02X", got[i]);
        }
    }
    LOGLN();
    return n;
}

uint32_t scan_production_frame()
{
    // The broad scan below established that this physical unit answers only at 9600.
    // Read the same contiguous five-register span Stage 1 will use so a baud-only reply
    // cannot be mistaken for a production-compatible sensor. This is diagnostic only:
    // its raw frame is the evidence gate for the production driver's selected baud. Refs #30.
    //
    // CITE(datasheet): [CIT-RK900] five consecutive holding registers from 0x0000.
    // CITE(sibling): forest-weather-machines (local sibling, ~/Documents/GitHub) —
    //   docs/RK900-09_BRINGUP_AND_FALLBACKS_2026-05-15.md records 9600 8N1, FC 0x03,
    //   slave 0x01, start 0x0000, quantity 5 as the bench procedure.
    constexpr uint32_t kObservedBaud = 9600;
    constexpr uint8_t kObservedSlave = 0x01;
    constexpr uint8_t kProductionRegisterCount = 5;

    Serial1.begin(kObservedBaud);
    delay(20);
    const uint32_t total = scan_one(kObservedBaud, kObservedSlave, kProductionRegisterCount);
    Serial1.end();
    return total;
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

    // Only test the full frame with the transceiver powered: an unpowered line is the
    // broad scan's control, not a separate claim about register contents.
    pinMode(WB_IO2, OUTPUT);
    digitalWrite(WB_IO2, HIGH);
    delay(50);
    const uint32_t production_frame = scan_production_frame();

    // Restored so the pin is not left driving the rail down between cycles.
    digitalWrite(WB_IO2, HIGH);

    LOGF("[bus scan] verdict: %lu byte(s) powered vs %lu unpowered; "
         "9600/0x01 production frame: %lu byte(s)\n",
         (unsigned long)powered, (unsigned long)unpowered,
         (unsigned long)production_frame);

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

#if FEATURE_ONEWIRE_SCAN
// One-wire equivalent of bus_scan() above, for the RAK9154 pack.
//
// The driver's verdict is "no reply, 0 bytes", which is produced whenever the line never
// goes low inside the first-byte window. That single outcome covers two faults with
// opposite fixes — the pack never hears the request, or the pack answers and the receiver
// misses it — so this measures the pin before it assumes any protocol, then sweeps the
// framing, and prints every raw byte whether it decodes or not.
//
// Diagnostic only. It does not call Battery at all and changes nothing about how the
// production driver behaves; it reports bytes, and never synthesises a reading from them.

// Same candidate set the RS-485 sweep uses, and for the same reason. The documented rate is
// listed first, but "documented" is exactly what the RK900's rate was too — and the sweep
// found that sensor answering at twice it (docs/decisions/ADR-0006). An assumption gets
// swept, not trusted.
// CITE(prior-art): [CIT-MESHTASTIC-9154] @ 02050a4 variants/rak2560/RAK9154Sensor.cpp —
//   `mySerial.begin(9600)`; the rate a working reader uses on this same pack, so it leads the
//   list and is the fallback for the phase-4 probe sweep.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 SoftwareHalfSerial::begin(long speed)
//   derives every delay from the requested speed, so the byte layer is baud-agnostic.
// CITE(spec): docs/FIRMWARE_SPEC.md §2.2 — 9600 half-duplex is the specified one-wire rate.
constexpr long kOwBauds[] = {9600, 4800, 19200, 38400, 115200};

// Phase 4 addresses. The driver hard-codes one destination and discards the BOOT reply, so if
// the pack announces itself as a different probe id then every data request has been going to
// the wrong address — a failure indistinguishable from silence.
// CITE(prior-art): [CIT-MESHTASTIC-9154] RAK9154Sensor.cpp requests data for the PID the
//   pack announces in its provision reply rather than a fixed id; 0x01 is only what a
//   single-probe hub happens to enumerate.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] onewire_master_protocol.h PID_UNKNOW = 0xFF is the
//   broadcast destination, which a pack that ignores unicast may still answer.
constexpr uint8_t kOwProbeIds[]  = {0x01, 0x02, 0x03, 0xFF};
constexpr uint8_t kOwBroadcast   = 0xFF; // CITE(prior-art): [CIT-ONEWIRE-SERIAL] PID_UNKNOW

// Windows. The census is long enough to catch an unsolicited report at a human cadence; the
// passive listen is longer still because nothing prompts it; the post-request windows match
// what the driver already allows.
// CITE(prior-art): [CIT-MESHTASTIC-9154] RAK9154Sensor.cpp drains with a delay(2) grace
//   period per byte, so a few hundred ms is far more than a whole frame needs once the
//   first byte has arrived; the generous first-byte allowance is what covers probe wake.
constexpr uint32_t kOwCensusMs  = 2000;
constexpr uint32_t kOwPassiveMs = 3000;

// Capture depth. 64 was silently the most consequential number in this scanner: the pack's
// provisioning announcement is a 92-byte frame whose *tail* carries the per-sensor sampling
// rules, so a 64-byte buffer captured the identity fields and threw away the only bytes that
// say whether the pack is sampling at all. Every earlier capture stopped 3 bytes short of the
// sensor count and 4 short of the first rule value.
//
// 0x100 is BUFF_SIZE in the reference master — the largest frame the protocol handles — and
// also comfortably holds the data reply and an announcement arriving back to back.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 src/onewire_master_protocol.c `#define
//   BUFF_SIZE 0x100`; onewire_master_protocol.h SNHub_Api_Provision_t places snsr_num after
//   sn[18] + provId + reserved1[7] + model_name[20] + reserved2[4], i.e. 54 bytes into the
//   provision payload, followed by 4-byte SNSRNODE { sid, ipso, U16 rule } descriptors.
// CITE(bench): docs/EVIDENCE.md — the announcement captured on 3d3425d declares hub
//   payload_length 0x50 (80) with the provision payload starting at frame index 12, putting
//   snsr_num at index 66 and the rule fields at 69, 73, 77, 81, 85 and 89 — all beyond 64.
constexpr size_t kOwCaptureBytes = 0x100;
constexpr uint32_t kOwBootMs    = 300; // CITE(prior-art): [CIT-MESHTASTIC-9154] as above
constexpr uint32_t kOwSendatMs  = 500; // CITE(prior-art): [CIT-MESHTASTIC-9154] as above

// RUI3 transport + SensorHub request fields. Deliberately duplicated from battery.cpp rather
// than exposed from it: this scan must be able to send frames the production driver never
// sends (other destinations, other bauds) without widening the driver's interface, and a
// diagnostic that shares mutable state with the thing it is diagnosing is not a diagnostic.
//
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 src/onewire_master_protocol.h —
//   WAKEUPBYTE 0xFF, DELIMTER 0x7E, RUI3API_TYPE_SENSORHUB 2, RUI3API_FLG_REQ 0,
//   PID_MASTER 0, SNHUB_TYPE_PROVISION 1, SNHUB_TYPE_SENDAT 3, PLD_PROVI_TYPE_BOOT 2,
//   PLD_SDATA_TPYE_SENDAT 2, and SHORT_SWAP(sizeof(SNHub_Api_t)) = 6 in wire order.
// CITE(spec): docs/FIRMWARE_SPEC.md §2.2 — the battery link is the RAK Sensor Hub one-wire
//   protocol; this scan probes that contract without altering it.
//
// One upstream symbol per line, so a reviewer can check each value against the header rather
// than against a paragraph.
constexpr uint8_t kOwWake      = 0xFF; // CITE(prior-art): [CIT-ONEWIRE-SERIAL] WAKEUPBYTE
constexpr uint8_t kOwDelimiter = 0x7E; // CITE(prior-art): [CIT-ONEWIRE-SERIAL] DELIMTER
constexpr uint8_t kOwWakeCount = 4;    // extra 0xFF settle the line; the pack scans for 0x7E
constexpr uint8_t kOwMaster    = 0x00; // CITE(prior-art): [CIT-ONEWIRE-SERIAL] PID_MASTER
constexpr uint8_t kOwRui3Type  = 0x02; // CITE(prior-art): [CIT-ONEWIRE-SERIAL] TYPE_SENSORHUB
constexpr uint8_t kOwRui3Flag  = 0x00; // CITE(prior-art): [CIT-ONEWIRE-SERIAL] RUI3API_FLG_REQ
constexpr uint8_t kOwLenLo     = 0x00; // CITE(prior-art): [CIT-ONEWIRE-SERIAL] SHORT_SWAP(6) lo
constexpr uint8_t kOwLenHi     = 0x06; // CITE(prior-art): [CIT-ONEWIRE-SERIAL] sizeof(SNHub_Api_t)
constexpr uint8_t kOwProvision = 0x01; // CITE(prior-art): [CIT-ONEWIRE-SERIAL] TYPE_PROVISION
constexpr uint8_t kOwSendData  = 0x03; // CITE(prior-art): [CIT-ONEWIRE-SERIAL] SNHUB_TYPE_SENDAT
constexpr uint8_t kOwPldBoot   = 0x02; // CITE(prior-art): [CIT-ONEWIRE-SERIAL] PROVI_TYPE_BOOT
constexpr uint8_t kOwPldSendat = 0x02; // CITE(prior-art): [CIT-ONEWIRE-SERIAL] SDATA_TPYE_SENDAT

// Running totals for the verdict at the end of the cycle.
uint32_t ow_edges_pulled = 0; // edges seen with the pull-up on — the meaningful count
uint32_t ow_edges_float  = 0; // edges seen with the pin floating — the control
uint32_t ow_bytes        = 0; // every byte captured in any phase
long     ow_best_baud    = 0; // first baud that produced a byte, 0 if none ever did

// One wire, one bus. The library keeps its RX ring buffer and `active_object` in class
// statics, so it is a singleton by construction.
SoftwareHalfSerial &ow_bus()
{
    static SoftwareHalfSerial instance(kBatteryPin);
    return instance;
}

void ow_dump(const uint8_t *b, uint32_t n)
{
    if (n == 0) {
        LOGLN();
        return;
    }
    LOG("  <-");
    for (uint32_t i = 0; i < n; i++) {
        LOGF(" %02X", b[i]);
    }
    LOGLN();
}

// Phase 1 — idle level and HIGH->LOW edge census. The highest-value measurement here,
// because it assumes nothing: no baud, no framing, no addressing. An open-drain bus idles
// high through a pull-up and every talker pulls it low, so a falling edge is the only
// unambiguous evidence that something other than this MCU drives the wire.
//
// Run in both pin modes on purpose, and the comparison is the whole point — it is the same
// control bus_scan() gets from powering the RS-485 transceiver down. With the pull-up on, an
// undriven line reads a steady HIGH and cannot produce edges, so any edge is a real driver.
// With the pull-up removed the pin floats and will happily count thousands of edges from
// nothing at all. Edges in the floating mode only means the wire is dead and the pin is
// picking up its own neighbourhood.
//
// CITE(datasheet): [CIT-NRF-GPIO] nRF52840 PS, GPIO — PIN_CNF[n].PULL is configured
//   independently of DIR, so INPUT_PULLUP and INPUT really do terminate an undriven line
//   differently; the pair is a measurement, not two names for the same thing.
// CITE(datasheet): [CIT-RAK19007] the one-wire link lands on the base board's WB_IO1 pad
//   (kBatteryPin above), which is the pin sampled here — pack pins 3+5 joined, confirmed
//   continuous to that pad.
uint32_t ow_census(bool pull_up)
{
    // The library arms a GPIOTE falling-edge interrupt on this same pin and would consume
    // the edges this phase is trying to count. Released first, deliberately.
    // CITE(datasheet): [CIT-NRF-GPIOTE] nRF52840 PS, GPIOTE — an IN event with
    //   CONFIG.POLARITY = HiToLo fires on exactly the transition counted below, so the
    //   receiver and this census cannot both own the pin.
    ow_bus().end();
    pinMode(kBatteryPin, pull_up ? INPUT_PULLUP : INPUT);
    delay(5);

    const int idle = digitalRead(kBatteryPin);

    uint32_t edges   = 0;
    uint32_t lows    = 0;
    uint32_t samples = 0;
    int      prev    = idle;

    const uint32_t start = millis();
    while ((millis() - start) < kOwCensusMs) {
        const int now = digitalRead(kBatteryPin);
        samples++;
        if (now == LOW) {
            lows++;
        }
        if (prev == HIGH && now == LOW) {
            edges++;
        }
        prev = now;
    }

    // The low-sample fraction separates three states a bare edge count cannot: a line held
    // low (stuck, or a talker that never releases), a line idling high with occasional
    // pulses (a healthy open-drain bus), and a line that is simply floating.
    LOGF("   %-14s idle %s : %lu falling edge(s), %lu of %lu samples LOW\n",
         pull_up ? "INPUT_PULLUP" : "INPUT (float)", idle == HIGH ? "HIGH" : "LOW ",
         (unsigned long)edges, (unsigned long)lows, (unsigned long)samples);

    power::watchdog_feed();
    return edges;
}

// Phase 2 — listen with the transmitter never used. The reference protocol has an
// unsolicited report path as well as a request/response one, so a pack can volunteer data
// without being asked. Anything captured here proves the pack is alive on the wire and
// moves the fault entirely onto our request framing or addressing.
//
// CITE(prior-art): [CIT-MESHTASTIC-9154] @ 02050a4 RAK9154Sensor.cpp handles
//   SNHUBAPI_EVT_SDATA_REPORT on a separate branch from SNHUBAPI_EVT_SDATA_REQ — the report
//   arrives unprompted, and its multi-byte values use the opposite byte order to the
//   requested path, which is why it is worth capturing raw rather than parsing here.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 SoftwareHalfSerial::begin() calls listen(),
//   which attaches the falling-edge interrupt; end() -> stopListening() detaches it and
//   resets the buffer, so end()-then-begin() is what makes a per-baud restart clean.
uint32_t ow_passive(long baud)
{
    SoftwareHalfSerial &link = ow_bus();
    link.end();
    link.begin(baud);
    link.flush();

    static uint8_t got[kOwCaptureBytes];
    uint32_t       n = 0;

    const uint32_t start = millis();
    while ((millis() - start) < kOwPassiveMs) {
        const int v = link.read();
        if (v >= 0 && n < sizeof(got)) {
            got[n++] = (uint8_t)v;
        }
    }
    link.end();

    LOGF("   %6lu baud  passive %lu ms : %lu byte(s)", (unsigned long)baud,
         (unsigned long)kOwPassiveMs, (unsigned long)n);
    ow_dump(got, n);

    if (n > 0 && ow_best_baud == 0) {
        ow_best_baud = baud;
    }
    ow_bytes += n;
    power::watchdog_feed();
    return n;
}

// Builds a zero-payload SensorHub request into `out` and returns its length. Same shape as
// the driver's send_frame(), including the checksum, which is NOT an XOR: it is the sum of
// the set-bit counts of the RUI3 type and flag bytes plus every byte the length field
// covers, accumulated into a uint8_t.
//
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.c cal_chksum():
//   chsum = popcount(type) + popcount(flag) + sum popcount(payload[0..len-1]); and
//   api_init() -> snhub_provision_command(PID_UNKNOW, SNHUB_GS_SET, PLD_PROVI_TYPE_BOOT)
//   for the BOOT broadcast, snhub_snsrdat_command() for SENDAT.
size_t ow_build(uint8_t *out, uint8_t dest, uint8_t hub_type, uint8_t payload_type,
                uint8_t seq)
{
    // CITE(prior-art): [CIT-ONEWIRE-SERIAL] SNHub_Api_t{dest, source, sequence, type,
    //   payload_length, payload_type} — payload_length is 0x00 for both requests sent here.
    const uint8_t hub[6] = {dest, kOwMaster, seq, hub_type, 0x00, payload_type};

    uint8_t checksum =
        (uint8_t)(__builtin_popcount(kOwRui3Type) + __builtin_popcount(kOwRui3Flag));
    for (size_t i = 0; i < sizeof(hub); i++) {
        checksum += __builtin_popcount(hub[i]);
    }

    size_t n = 0;
    for (uint8_t i = 0; i < kOwWakeCount; i++) {
        out[n++] = kOwWake;
    }
    out[n++] = kOwDelimiter;
    out[n++] = kOwLenLo;
    out[n++] = kOwLenHi;
    out[n++] = kOwRui3Type;
    out[n++] = kOwRui3Flag;
    for (size_t i = 0; i < sizeof(hub); i++) {
        out[n++] = hub[i];
    }
    out[n++] = checksum;
    return n;
}

// Sends one request and dumps whatever comes back, valid or not. A malformed reply is the
// most informative result this scan can produce: it proves the pack hears us and leaves only
// the framing to fix.
//
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 SoftwareHalfSerial::write() turns the line
//   around per byte — beginTx() detaches the RX interrupt and drives the pin, beginRx()
//   restores input-with-pull-up — so the pack can answer the instant we stop talking and our
//   own start bits never re-trigger our own receiver.
uint32_t ow_request(long baud, const char *label, uint8_t dest, uint8_t hub_type,
                    uint8_t payload_type, uint32_t listen_ms)
{
    static uint8_t seq = 0;

    SoftwareHalfSerial &link = ow_bus();
    link.end();
    link.begin(baud);
    link.flush();

    uint8_t      tx[20];
    const size_t tx_len = ow_build(tx, dest, hub_type, payload_type, ++seq);
    for (size_t i = 0; i < tx_len; i++) {
        link.write(tx[i]);
    }
    delay(2); // let the probe turn the line around

    static uint8_t got[kOwCaptureBytes];
    uint32_t       n = 0;

    const uint32_t start = millis();
    while ((millis() - start) < listen_ms) {
        const int v = link.read();
        if (v >= 0 && n < sizeof(got)) {
            got[n++] = (uint8_t)v;
        }
    }
    link.end();

    LOGF("   %6lu baud  %s dest 0x%02X : %lu byte(s)", (unsigned long)baud, label, dest,
         (unsigned long)n);
    ow_dump(got, n);

    if (n > 0 && ow_best_baud == 0) {
        ow_best_baud = baud;
    }
    ow_bytes += n;
    power::watchdog_feed();
    return n;
}

void onewire_scan()
{
    LOGF("[ow scan] pin WB_IO1 (Arduino %u), pack pins 3+5 joined\n", (unsigned)kBatteryPin);

    LOGLN(F("[ow scan] phase 1: idle level and falling-edge census, no UART, no framing"));
    ow_edges_pulled += ow_census(true);
    ow_edges_float  += ow_census(false);

    LOGLN(F("[ow scan] phase 2: passive listen, nothing transmitted"));
    for (uint32_t i = 0; i < (sizeof(kOwBauds) / sizeof(kOwBauds[0])); i++) {
        ow_passive(kOwBauds[i]);
    }

    LOGLN(F("[ow scan] phase 3: BOOT/provision broadcast, swept across bauds"));
    for (uint32_t i = 0; i < (sizeof(kOwBauds) / sizeof(kOwBauds[0])); i++) {
        ow_request(kOwBauds[i], "BOOT  ", kOwBroadcast, kOwProvision, kOwPldBoot, kOwBootMs);
    }

    // Whichever baud produced a byte, or the documented rate if none did. Sweeping the
    // probe ids at every baud as well would quadruple the cycle for no new information:
    // phase 3 has already established which baud, if any, the pack hears.
    const long probe_baud = (ow_best_baud != 0) ? ow_best_baud : kOwBauds[0];
    LOGF("[ow scan] phase 4: SENDAT probe-id sweep at %lu baud%s\n", (unsigned long)probe_baud,
         (ow_best_baud != 0) ? " (first baud that answered)" : " (no baud answered; documented rate)");
    for (uint32_t i = 0; i < (sizeof(kOwProbeIds) / sizeof(kOwProbeIds[0])); i++) {
        ow_request(probe_baud, "SENDAT", kOwProbeIds[i], kOwSendData, kOwPldSendat,
                   kOwSendatMs);
    }

    // Leave the pin as a plain input: the library idles RX with the pull-up on, and holding
    // that costs current through the pack's line resistor for as long as it is held.
    // CITE(datasheet): [CIT-NRF-GPIO] nRF52840 PS, GPIO — PIN_CNF[n].PULL survives releasing
    //   the pin, so re-running pinMode(INPUT) after end() is what actually removes it.
    ow_bus().end();
    pinMode(kBatteryPin, INPUT);

    LOGF("[ow scan] verdict: %lu pulled-up edge(s), %lu floating edge(s), %lu byte(s) total\n",
         (unsigned long)ow_edges_pulled, (unsigned long)ow_edges_float,
         (unsigned long)ow_bytes);

    if (ow_edges_pulled == 0 && ow_bytes == 0) {
        LOGLN(F("[ow scan] nothing ever pulled this line low. With the pull-up on, an undriven"));
        LOGLN(F("          line cannot produce an edge, so the pack is not driving the wire at"));
        LOGLN(F("          all. No firmware change can alter that. The remaining suspects are"));
        LOGLN(F("          physical: which pack pin the data line actually is, whether pins 3+5"));
        LOGLN(F("          are the pair, and whether the pack's BMS wakes on this bus."));
        if (ow_edges_float > 0) {
            LOGLN(F("          The floating-pin edges above are the control, not a reply — an"));
            LOGLN(F("          unterminated input counts edges from nothing. Ignore them."));
        }
    } else if (ow_bytes == 0) {
        LOGLN(F("[ow scan] the line IS being pulled low, but no byte ever framed. The pack is"));
        LOGLN(F("          driving the wire and the remaining variable is the baud or the bit"));
        LOGLN(F("          timing, not the wiring. Read the edge counts above: their rate over"));
        LOGLN(F("          2000 ms bounds the plausible baud before changing any constant."));
    } else {
        LOGLN(F("[ow scan] bytes arrived. The pack talks, so the fault is framing or addressing,"));
        LOGLN(F("          not the wire. Read the hex above before changing any constant —"));
        LOGLN(F("          which phase produced it says whether the pack answers unprompted,"));
        LOGLN(F("          answers a BOOT, or answers a SENDAT, and at which probe id."));
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

#if FEATURE_ONEWIRE_SCAN
    // Same deal for the battery link: measure, print, and stop. No reading is produced and
    // no uplink is built, so nothing downstream can dress a raw byte up as a voltage.
    onewire_scan();
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
    } else {
        // Counted here, unconditionally, so a brownout hold (below) still advances the
        // quiet-cycle count. It previously lived in the heartbeat branch's condition, where
        // short-circuit evaluation skipped the increment whenever the brownout branch was
        // taken instead — a silent-and-low-power node under-reported how long it had been
        // silent in the first log line after recovery. Refs #24.
        ++empty_cycles;
    }

    uint32_t sleep_for = config.interval_seconds();

#if FEATURE_RADIO
    if (!brownout.transmit_allowed()) {
        // Deliberately still reading the sensors and still waking on schedule. The pack
        // recovers on sunlight, not on being left alone, and the node has to notice the
        // moment it can transmit again.
        LOGLN(F("   uplink  : held — pack too low to transmit"));
    } else if (payload.empty() && !heartbeat_due(empty_cycles)) {
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

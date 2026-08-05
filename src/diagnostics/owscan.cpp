#include "owscan.h"

#if FEATURE_ONEWIRE_SCAN

#include <Arduino.h>
#include <SoftwareHalfSerial.h>

namespace diagnostics {
namespace {

// The pin under test, handed in by the caller rather than redeclared here. main.cpp already
// owns this fact and cites the datasheet for it; a second copy is a divergence waiting to
// happen, which is the same failure the wake-byte count already demonstrated.
uint8_t g_pin = 0;

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
constexpr uint8_t kOwWakeCount = 1;    // must equal Battery::kWakeCount -- see owscan.h
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
    static SoftwareHalfSerial instance(g_pin);
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
//   (g_pin above), which is the pin sampled here — pack pins 3+5 joined, confirmed
//   continuous to that pad.
uint32_t ow_census(bool pull_up)
{
    // The library arms a GPIOTE falling-edge interrupt on this same pin and would consume
    // the edges this phase is trying to count. Released first, deliberately.
    // CITE(datasheet): [CIT-NRF-GPIOTE] nRF52840 PS, GPIOTE — an IN event with
    //   CONFIG.POLARITY = HiToLo fires on exactly the transition counted below, so the
    //   receiver and this census cannot both own the pin.
    ow_bus().end();
    pinMode(g_pin, pull_up ? INPUT_PULLUP : INPUT);
    delay(5);

    const int idle = digitalRead(g_pin);

    uint32_t edges   = 0;
    uint32_t lows    = 0;
    uint32_t samples = 0;
    int      prev    = idle;

    const uint32_t start = millis();
    while ((millis() - start) < kOwCensusMs) {
        const int now = digitalRead(g_pin);
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

} // namespace

void onewire_scan(uint8_t pin)
{
    g_pin = pin;

    LOGF("[ow scan] pin WB_IO1 (Arduino %u), pack pins 3+5 joined\n", (unsigned)g_pin);

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
    pinMode(g_pin, INPUT);

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

} // namespace diagnostics

#endif // FEATURE_ONEWIRE_SCAN

#include "busscan.h"

#if FEATURE_BUS_SCAN

#include "../sensors/crc16.h"
#include "../power.h"

#include <Arduino.h>

namespace diagnostics {
namespace {

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

} // namespace

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

} // namespace diagnostics

#endif // FEATURE_BUS_SCAN

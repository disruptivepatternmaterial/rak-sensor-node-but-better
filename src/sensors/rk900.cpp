#include "rk900.h"

#include "../build_features.h"

namespace {

// CITE(bench): [CIT-RK900-BAUD-2026-08-03] busscan bus scan at multiple rates; valid reply at
//   9600, none at 4800, on this physical unit.
// ADR-0006 has the full picture: this is the sensor's Rika factory default and matches an
// independent open-source integration ([CIT-BEEGEE-RS485-WIND]), but the one real fleet
// precedent for this exact sensor+battery combo runs it at 4800 instead
// ([CIT-FWM-RK900-FIELD]). The sensor's baud is field-settable in place — see ADR-0006's
// reprovisioning sequence — so this is a fleet-consistency question to revisit after the
// full five-register frame is read, not a hardware constraint.
constexpr uint8_t  kSlave = 0x01;
constexpr uint32_t kBaud  = 9600;

// CITE(datasheet): [CIT-RK900] five consecutive holding registers from 0x0000. Read as one
//   span rather than five transactions: one bus turnaround instead of five, which is both
//   faster and cheaper in the power budget.
constexpr uint16_t kFirstRegister = 0x0000;
constexpr uint8_t  kRegisterCount = 5;

enum RegisterIndex : uint8_t {
    kWindSpeed     = 0, // x0.01 m/s
    kWindDirection = 1, // degrees, raw
    kTemperature   = 2, // x0.1 degC, signed
    kHumidity      = 3, // x0.1 %RH
    kPressure      = 4, // x0.1 hPa
};

// CITE(datasheet): [CIT-RAK5802] the transceiver runs from the switched 3V3_S rail. It
//   needs a moment after power-up before the driver is stable enough to transmit.
constexpr uint32_t kTransceiverSettleMs = 20;

} // namespace

void RK900::power_on()
{
    pinMode(WB_IO2, OUTPUT);
    digitalWrite(WB_IO2, HIGH);
    delay(kTransceiverSettleMs);

    Serial1.begin(kBaud);
}

void RK900::power_off()
{
    // Both halves matter for the power budget. Closing the UART stops the nRF52 keeping
    // the peripheral clocked through sleep, and dropping WB_IO2 removes the transceiver's
    // idle draw. Leaving either on is worth roughly a milliamp, which dominates everything
    // else at an hourly interval.
    // CITE(datasheet): [CIT-NRF-PERIPH-SLEEP] measured 1.2 mA / 890 uA down to 10 uA by
    //   disabling UART and SPI before sleeping.
    Serial1.end();
    digitalWrite(WB_IO2, LOW);
}

WeatherReading RK900::read()
{
    WeatherReading out;

    power_on();

    ModbusMaster bus(Serial1, kBaud);
    uint16_t     regs[kRegisterCount] = {0};

    m_last = bus.read_holding(kSlave, kFirstRegister, kRegisterCount, regs);

    power_off();

    if (m_last != ModbusResult::Ok) {
        LOGF("   RK900   : no data (%s)\n", modbus_result_name(m_last));
        return out; // every field stays invalid — the encoder will omit them all
    }

    // Raw dump first, ahead of any interpretation. ADR-0006 documents a genuine conflict
    // between two RK900-09 sources on what 0x0000-0x0004 actually mean; printing the words
    // as received lets that be re-checked against either candidate map without re-wiring
    // anything, and costs nothing once a map is settled.
    LOGF("   RK900   : raw 0x0000-0x0004 = %04X %04X %04X %04X %04X\n",
         regs[0], regs[1], regs[2], regs[3], regs[4]);

    // A CRC-valid frame is not the same thing as a measurement.
    //
    // Every field of an all-zero span is a physical claim, and one of them cannot be true:
    // 0.0 hPa is a vacuum, not weather. The whole span is therefore refused rather than
    // encoded, which is the same judgement the battery path already makes on the pack's
    // all-zero record template — and for the same reason, since a node that wakes, reads once
    // and sleeps for an hour has no second chance to notice. A gap in the series is
    // recoverable; a plausible wrong number is not.
    //
    // Judging the whole span keeps every genuine zero: 0 m/s is calm, 0 degrees is due north,
    // and 0.0 degC is an ordinary temperature in the woods. Those survive as long as one other
    // register is non-zero, which for a working station is always true of pressure.
    //
    // CITE(datasheet): [CIT-RK900] RK900-09 register map — register 0x0004 is barometric
    //   pressure at x0.1 hPa, so the value 0 decodes to 0.0 hPa; the sensor cannot be in a
    //   vacuum and still be reporting.
    // CITE(spec): docs/FIRMWARE_SPEC.md §2.1 null policy — "Never invent 0". A missing read
    //   is omitted from the payload, not encoded as a zero.
    // CITE(bench): docs/EVIDENCE.md — the RAK9154 has been captured returning a checksum-valid
    //   all-zero record while demonstrably alive, which is why battery_frame.cpp refuses it
    //   (BatteryResult::Unsampled). The RK900 had no equivalent guard.
    if ((regs[kWindSpeed] | regs[kWindDirection] | regs[kTemperature] | regs[kHumidity] |
         regs[kPressure]) == 0) {
        m_last = ModbusResult::Unsampled;
        LOGLN(F("   RK900   : CRC-valid but all five registers are zero — no reading "
                "(0.0 hPa is not weather)"));
        return out; // every field stays invalid — the encoder will omit them all
    }

    // The span either arrives whole and CRC-checked or not at all, so the fields become
    // valid together. Values are stored exactly as the sensor reported them; scaling
    // belongs to the encoder, which has to match the TTN decoder's divisors.
    out.wind_speed.set(regs[kWindSpeed]);
    out.wind_direction.set(regs[kWindDirection]);
    out.temperature.set((int16_t)regs[kTemperature]);
    out.humidity.set(regs[kHumidity]);

    // Pressure alone, for the mixed case the whole-span test cannot catch: a station reporting
    // real wind and a 0.0 hPa barometer has a broken barometer, not a vacuum. The field is left
    // null and the rest of the reading still goes out.
    // CITE(datasheet): [CIT-RK900] register 0x0004, x0.1 hPa.
    // CITE(spec): docs/FIRMWARE_SPEC.md §2.1 — missing field omitted, never encoded as 0.
    if (regs[kPressure] == 0) {
        LOGLN(F("   RK900   : pressure register reads 0 — omitting the field, not encoding it"));
    } else {
        out.pressure.set(regs[kPressure]);
    }

    // The summary must say what the payload says. Printing `0.0 hPa` on the line after
    // deciding not to encode the field put a measured-looking pressure in the bench capture
    // that the uplink does not carry, and the next person to compare the two starts debugging
    // the decoder or the register map instead of the barometer. A console line that
    // contradicts the wire is worse than no console line.
    // CITE(spec): docs/FIRMWARE_SPEC.md §2.1 null policy — a `0` pressure register is
    //   omitted, and "0.0 hPa is a vacuum, not weather".
    // CITE(bench): docs/EVIDENCE.md — evidence is the raw observation; a diagnostic that
    //   disagrees with the encoded payload is not one.
    // CITE(policy): AGENTS.md — null sensor readings stay null and zeros are never
    //   fabricated. A console line that prints one is the same fabrication in another place.
    if (out.pressure.valid) {
        LOGF("   RK900   : wind %u.%02u m/s @ %u deg, %d.%d C, %u.%u %%RH, %u.%u hPa\n",
             regs[kWindSpeed] / 100, regs[kWindSpeed] % 100,
             regs[kWindDirection],
             (int16_t)regs[kTemperature] / 10, abs((int16_t)regs[kTemperature] % 10),
             regs[kHumidity] / 10, regs[kHumidity] % 10,
             regs[kPressure] / 10, regs[kPressure] % 10);
    } else {
        LOGF("   RK900   : wind %u.%02u m/s @ %u deg, %d.%d C, %u.%u %%RH, pressure null\n",
             regs[kWindSpeed] / 100, regs[kWindSpeed] % 100,
             regs[kWindDirection],
             (int16_t)regs[kTemperature] / 10, abs((int16_t)regs[kTemperature] % 10),
             regs[kHumidity] / 10, regs[kHumidity] % 10);
    }

    return out;
}

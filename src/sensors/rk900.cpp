#include "rk900.h"

#include "../build_features.h"

namespace {

// CITE(datasheet): [CIT-RK900] Modbus slave address and line rate. Fixed, not configurable
//   — ADR-0004 dedicated the RAK5802 to this sensor precisely so it could stay fixed.
constexpr uint8_t  kSlave = 0x01;
constexpr uint32_t kBaud  = 4800;

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

    // The span either arrives whole and CRC-checked or not at all, so all five fields
    // become valid together. Values are stored exactly as the sensor reported them;
    // scaling belongs to the encoder, which has to match the TTN decoder's divisors.
    out.wind_speed.set(regs[kWindSpeed]);
    out.wind_direction.set(regs[kWindDirection]);
    out.temperature.set((int16_t)regs[kTemperature]);
    out.humidity.set(regs[kHumidity]);
    out.pressure.set(regs[kPressure]);

    LOGF("   RK900   : wind %u.%02u m/s @ %u deg, %d.%d C, %u.%u %%RH, %u.%u hPa\n",
         regs[kWindSpeed] / 100, regs[kWindSpeed] % 100,
         regs[kWindDirection],
         (int16_t)regs[kTemperature] / 10, abs((int16_t)regs[kTemperature] % 10),
         regs[kHumidity] / 10, regs[kHumidity] % 10,
         regs[kPressure] / 10, regs[kPressure] % 10);

    return out;
}

#include "payload.h"

namespace {

// Channel and type pairs, mirroring payload/schema.yaml exactly. Both numbers are part of
// the contract: the decoder looks up behavior by type and names the output by channel.
constexpr uint8_t kChWindSpeed = 1, kTyWindSpeed = 190;
constexpr uint8_t kChWindDir   = 2, kTyWindDir   = 191;
constexpr uint8_t kChAirTemp   = 3, kTyAirTemp   = 103;
constexpr uint8_t kChHumidity  = 4, kTyHumidity  = 112;
constexpr uint8_t kChPressure  = 5, kTyPressure  = 115;

constexpr uint8_t kChBattVolts = 21, kTyBattVolts = 186;
constexpr uint8_t kChBattAmps  = 22, kTyBattAmps  = 185;
constexpr uint8_t kChBattSoc   = 23, kTyBattSoc   = 184;
constexpr uint8_t kChBattTemp  = 24, kTyBattTemp  = 103;

} // namespace

void Payload::put_u8(uint8_t channel, uint8_t type, uint8_t value)
{
    if (!room_for(3)) {
        return;
    }
    m_buf[m_len++] = channel;
    m_buf[m_len++] = type;
    m_buf[m_len++] = value;
}

void Payload::put_u16(uint8_t channel, uint8_t type, uint16_t value)
{
    if (!room_for(4)) {
        return;
    }
    m_buf[m_len++] = channel;
    m_buf[m_len++] = type;
    m_buf[m_len++] = (uint8_t)(value >> 8);
    m_buf[m_len++] = (uint8_t)(value & 0xFF);
}

void Payload::put_s16(uint8_t channel, uint8_t type, int16_t value)
{
    // Two's complement on the wire; the decoder sign-extends from 16 bits.
    put_u16(channel, type, (uint16_t)value);
}

void Payload::add(const WeatherReading &w)
{
    // Every value below is passed through unscaled. The sensor's native scaling already
    // matches the divisor the decoder applies for each type, so converting here would
    // double-apply it. The one place that is not true is battery temperature, below.
    if (w.wind_speed.valid) {
        put_u16(kChWindSpeed, kTyWindSpeed, w.wind_speed.value);
    }

    // Raw heading, deliberately. The decoder applies the site's mounting offset, so
    // correcting here as well would rotate the reading twice. The decoder also nulls
    // direction when speed is zero, which is its policy to apply, not ours to pre-empt.
    if (w.wind_direction.valid) {
        put_u16(kChWindDir, kTyWindDir, w.wind_direction.value);
    }

    if (w.temperature.valid) {
        put_s16(kChAirTemp, kTyAirTemp, w.temperature.value);
    }

    // Type 112, never 104. Both are called "humidity" but 104 is a single byte with a
    // different divisor, and it decodes to a key that nothing downstream reads. The value
    // would arrive intact and then quietly go nowhere.
    if (w.humidity.valid) {
        put_u16(kChHumidity, kTyHumidity, w.humidity.value);
    }

    if (w.pressure.valid) {
        put_u16(kChPressure, kTyPressure, w.pressure.value);
    }
}

void Payload::add(const BatteryReading &b)
{
    if (b.voltage.valid) {
        put_u16(kChBattVolts, kTyBattVolts, b.voltage.value);
    }

    // Encoded exactly as the pack reported it. Whether a negative number means charging
    // or discharging is genuinely unsettled — the firmware spec and the decoder's own
    // header state opposite conventions (ADR-0002). That is a question about what the
    // number means, not about what to transmit, so the value goes out unmodified and the
    // interpretation gets resolved once the pack has been watched through a charge cycle.
    // Nothing on the node makes a decision from this field until then.
    if (b.current.valid) {
        put_s16(kChBattAmps, kTyBattAmps, b.current.value);
    }

    if (b.soc.valid) {
        put_u8(kChBattSoc, kTyBattSoc, b.soc.value);
    }

    // Type 103 carries tenths of a degree. The pack's one-wire records use the same IPSO
    // type as its own LoRaWAN uplinks, so the value is assumed to arrive in tenths already
    // and passes through. Note the Modbus register for the same measurement is whole
    // degrees and would need a factor of ten here — worth confirming against a known
    // ambient reading the first time the pack answers, since a wrong guess shows up as a
    // plausible-looking temperature that is off by 10x.
    if (b.temperature.valid) {
        put_s16(kChBattTemp, kTyBattTemp, b.temperature.value);
    }
}

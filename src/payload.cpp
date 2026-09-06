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
        m_dropped++;
        return;
    }
    m_buf[m_len++] = channel;
    m_buf[m_len++] = type;
    m_buf[m_len++] = value;
}

void Payload::put_u16(uint8_t channel, uint8_t type, uint16_t value)
{
    if (!room_for(4)) {
        m_dropped++;
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

// The only encoder. A second one, `add()`, used to sit above this in field order and was
// reachable from nothing in src/ — build() never called it and neither did anything else. It
// was not harmless dead code: it carried its own copy of every channel/type pairing plus the
// reasoning below, so it read as authoritative while never running, and a width or type fix
// applied to one of the two would have left the other silently wrong. Worse, it hid the drift
// it would have caused: check_decoder_parity.py scans this file for put_*() calls and
// deduplicates them, so two encoders that disagreed would have presented to the gate as the
// union of both and passed. Deleted; its comments are folded in here, where they govern bytes
// that actually leave the node.
void Payload::build(const WeatherReading &w, const BatteryReading &b, size_t budget)
{
    clear();
    m_budget = (budget == 0 || budget > kMaxPayloadBytes) ? kMaxPayloadBytes : budget;

    // Every value is passed through unscaled. The sensor's native scaling already matches the
    // divisor the decoder applies for each type, so converting here would double-apply it.
    // check_decoder_parity.py enforces this: an expression rather than a bare field in any
    // put_*() call fails the gate, because a factor applied on both sides arrives as a
    // plausible-looking wrong number rather than as a failure.

    // Priority order. The first three are the ones that fit even at the slowest data rate:
    // whether the node is dying, and the two readings the station exists to take.
    if (b.soc.valid) {
        put_u8(kChBattSoc, kTyBattSoc, b.soc.value);
    }
    if (w.wind_speed.valid) {
        put_u16(kChWindSpeed, kTyWindSpeed, w.wind_speed.value);
    }
    if (w.temperature.valid) {
        put_s16(kChAirTemp, kTyAirTemp, w.temperature.value);
    }

    // Everything past here is dropped first when the link is slow.
    if (b.voltage.valid) {
        put_u16(kChBattVolts, kTyBattVolts, b.voltage.value);
    }

    // Raw heading, deliberately. The decoder applies the site's mounting offset, so correcting
    // here as well would rotate the reading twice. The decoder also nulls direction when speed
    // is zero, which is its policy to apply, not ours to pre-empt.
    if (w.wind_direction.valid) {
        put_u16(kChWindDir, kTyWindDir, w.wind_direction.value);
    }

    // Type 112, never 104. Both are called "humidity" but 104 is a single byte with a
    // different divisor, and it decodes to a key that nothing downstream reads. The value
    // would arrive intact and then quietly go nowhere. See ADR-0002.
    if (w.humidity.valid) {
        put_u16(kChHumidity, kTyHumidity, w.humidity.value);
    }

    if (w.pressure.valid) {
        put_u16(kChPressure, kTyPressure, w.pressure.value);
    }

    // Encoded exactly as the pack reported it, and that is the decided convention rather than a
    // deferral: POSITIVE = charging, NEGATIVE = discharging (ADR-0002, 2026-08-13). Adopting the
    // pack's own convention means no sign transform exists anywhere between the pack's register
    // and the TTN record, so there is no site at which the record can be silently inverted. Do
    // not add one here.
    // CITE(spec): [CIT-CAYENNE-LPP] Cayenne LPP — type 185 signed 16-bit, 0.01 A units.
    // CITE(sibling): [CIT-FWM-DECODER] rak-wx-station-default.js @
    //   efc0e3cf25b3f9288ff1b9a1a60849b8d425cc32 — WX_TYPES[185] signed, divisor 100,
    //   no negation in arrayToDecimal; header :16 documents positive = charging. The
    //   decoder therefore needed no change to match this decision.
    if (b.current.valid) {
        put_s16(kChBattAmps, kTyBattAmps, b.current.value);
    }

    // Type 103 carries tenths of a degree. The pack's one-wire records use the same IPSO type
    // as its own LoRaWAN uplinks, and the 2026-08-05 capture (raw t=220 against a pack
    // reporting 22.0 C) confirms tenths, so the value passes through unscaled. The pack's
    // *Modbus* register for the same measurement is whole degrees and would need a factor of
    // ten — that path is not in use, and this note exists so nobody reintroduces the factor
    // from the wrong register map.
    if (b.temperature.valid) {
        put_s16(kChBattTemp, kTyBattTemp, b.temperature.value);
    }
}

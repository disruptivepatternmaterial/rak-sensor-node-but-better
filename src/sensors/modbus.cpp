#include "modbus.h"

#include "../build_features.h"

namespace {

// Longest reply this node ever asks for: address + function + byte count + 2 bytes per
// register + CRC. Sized for the 21-register battery span in case the Modbus fallback path
// is ever used (docs/FIRMWARE_SPEC.md §2.2).
constexpr size_t kMaxRegisters = 21;
constexpr size_t kMaxFrame     = 5 + (kMaxRegisters * 2);

// CITE(spec): [CIT-MODBUS-SERIAL] frames are separated by silence of at least 3.5
//   character times, floored at 1750 us for rates above 19200.
//
// The standard's character is 11 bits because its default framing carries a parity bit.
// This link runs 8N1, which is 10 bits, so 11 overstates the gap by a tenth. That is
// deliberate: the cost is 0.7 ms of extra silence per transaction at 4800 baud, and the
// benefit is margin against a slave whose idea of the gap is slightly longer than ours.
// Waiting too long is invisible; not waiting long enough corrupts the next frame.
uint32_t frame_gap_us(uint32_t baud)
{
    const uint32_t gap = (uint32_t)((3.5f * 11.0f * 1000000.0f) / (float)baud) + 1;
    return gap < 1750 ? 1750 : gap;
}

// Per-transaction ceiling from docs/FIRMWARE_SPEC.md §2.1. A silent sensor must cost a
// bounded amount of time and current, never the cycle.
constexpr uint32_t kReplyTimeoutMs = 1000;

} // namespace

const char *modbus_result_name(ModbusResult r)
{
    switch (r) {
    case ModbusResult::Ok:        return "ok";
    case ModbusResult::Timeout:   return "timeout";
    case ModbusResult::BadCrc:    return "bad crc";
    case ModbusResult::BadFrame:  return "bad frame";
    case ModbusResult::Exception: return "exception";
    }
    return "?";
}

void ModbusMaster::drain_and_settle()
{
    while (m_serial.available()) {
        (void)m_serial.read();
    }
    delayMicroseconds(frame_gap_us(m_baud));
}

ModbusResult ModbusMaster::transact(uint8_t slave, uint16_t start, uint8_t count,
                                    uint16_t *out)
{
    // CITE(spec): [CIT-MODBUS-APP] function 0x03 request is
    //   address | 0x03 | start hi | start lo | count hi | count lo | crc lo | crc hi.
    uint8_t req[8];
    req[0] = slave;
    req[1] = 0x03;
    req[2] = (uint8_t)(start >> 8);
    req[3] = (uint8_t)(start & 0xFF);
    req[4] = 0x00;
    req[5] = count;

    const uint16_t crc = modbus_crc16(req, 6);
    req[6] = (uint8_t)(crc & 0xFF);
    req[7] = (uint8_t)(crc >> 8);

    drain_and_settle();
    m_serial.write(req, sizeof(req));

    // The RAK5802 switches the driver off its own timing, but the UART must have actually
    // clocked the last bit out before the slave will answer.
    m_serial.flush();

    const size_t   expected = 5 + ((size_t)count * 2);
    uint8_t        resp[kMaxFrame];
    size_t         got   = 0;
    const uint32_t start_ms = millis();

    // Read until the frame is complete or the ceiling expires. Deliberately not
    // `while (!available())` — that is the shape that hangs forever when a sensor dies.
    //
    // The gap between bytes is watched as well as the total. Modbus ends a frame at three
    // and a half character times of silence, so a longer gap means the reply finished and
    // whatever arrives next belongs to a different frame. Without this check the two get
    // concatenated and the result is rejected on its checksum — the same outcome, but a
    // whole second later, and reported as a timeout rather than as the framing error it is.
    const uint32_t gap_limit_us = frame_gap_us(m_baud);
    uint32_t       last_byte_us = micros();

    while ((millis() - start_ms) < kReplyTimeoutMs && got < expected) {
        if (m_serial.available()) {
            resp[got++]  = (uint8_t)m_serial.read();
            last_byte_us = micros();
        } else if (got > 0 && (micros() - last_byte_us) > gap_limit_us) {
            // The slave stopped talking mid-frame. Retrying immediately is better than
            // waiting out the ceiling, because the line is already quiet.
            return ModbusResult::BadFrame;
        } else {
            delay(1);
        }
    }

    if (got < 3) {
        return ModbusResult::Timeout;
    }

    // CITE(spec): [CIT-MODBUS-APP] an error reply sets the high bit of the function code
    //   and carries a one-byte exception code, in a five-byte frame.
    //
    // Verified before it is believed, because an exception is treated as a final answer
    // and stops the retries. A single corrupted byte with the high bit set would otherwise
    // end the transaction and throw away every weather field for that cycle — the sensor
    // would look like it was refusing, when it never said anything of the kind.
    if (resp[1] & 0x80) {
        if (got < 5) {
            return ModbusResult::BadFrame;
        }

        const uint16_t want = modbus_crc16(resp, 3);
        const uint16_t have = (uint16_t)resp[3] | ((uint16_t)resp[4] << 8);
        if (resp[0] != slave || want != have) {
            return ModbusResult::BadFrame; // retryable — probably line noise, not a refusal
        }

        LOGF("      modbus exception 0x%02X from slave %u\n", resp[2], slave);
        return ModbusResult::Exception;
    }

    if (got < expected) {
        return ModbusResult::Timeout;
    }
    if (resp[0] != slave || resp[1] != 0x03 || resp[2] != (uint8_t)(count * 2)) {
        return ModbusResult::BadFrame;
    }

    const uint16_t want = modbus_crc16(resp, expected - 2);
    const uint16_t have = (uint16_t)resp[expected - 2] | ((uint16_t)resp[expected - 1] << 8);
    if (want != have) {
        return ModbusResult::BadCrc;
    }

    for (uint8_t i = 0; i < count; i++) {
        out[i] = ((uint16_t)resp[3 + (i * 2)] << 8) | (uint16_t)resp[4 + (i * 2)];
    }
    return ModbusResult::Ok;
}

ModbusResult ModbusMaster::read_holding(uint8_t slave, uint16_t start, uint8_t count,
                                        uint16_t *out, uint8_t retries)
{
    if (count == 0 || count > kMaxRegisters) {
        return ModbusResult::BadFrame;
    }

    ModbusResult result = ModbusResult::Timeout;
    for (uint8_t attempt = 0; attempt <= retries; attempt++) {
        result = transact(slave, start, count, out);
        if (result == ModbusResult::Ok || result == ModbusResult::Exception) {
            return result;
        }
        // Counted in attempts, not retries. The first pass is not a retry, and the last
        // failure is not followed by one — the earlier wording printed "retry 3/2" after
        // the final attempt, which reads like the retry limit was exceeded when the limit
        // was being honoured exactly.
        LOGF("      modbus attempt %u/%u failed (%s)\n", attempt + 1, retries + 1,
             modbus_result_name(result));
    }
    return result;
}

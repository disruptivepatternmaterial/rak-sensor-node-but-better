#include "battery.h"

#include "../build_features.h"

#include <Arduino.h>

namespace {

// CITE(prior-art): [CIT-ONEWIRE-SERIAL] WAKEUPBYTE 0xFF and DELIMTER 0x7E.
constexpr uint8_t kWakeByte  = 0xFF;
constexpr uint8_t kDelimiter = 0x7E;
constexpr uint8_t kWakeCount = 4;

// CITE(prior-art): [CIT-MESHTASTIC-9154] probe addressing on the Sensor Hub bus.
constexpr uint8_t kProbeId  = 0x01;
constexpr uint8_t kMasterId = 0x00;

// Request "send your latest sensor data".
constexpr uint8_t kTypeSendData    = 0x03;
constexpr uint8_t kPayloadSendData = 0x02;

// CITE(prior-art): [CIT-ONEWIRE-SERIAL] IPSO codes, already reduced by the 3200 offset.
//   These are the same numbers the payload encoder uses on the LoRaWAN side, because RAK
//   reuses the IPSO table in both places — which is a convenience, not a coincidence.
constexpr uint8_t kIpsoTemperature = 103; // 3303 - 3200
constexpr uint8_t kIpsoCapacity    = 184; // 3384 - 3200
constexpr uint8_t kIpsoDcCurrent   = 185; // 3385 - 3200
constexpr uint8_t kIpsoDcVoltage   = 186; // 3386 - 3200

// 9600 8N1, bit-banged because the line is a single open-drain wire shared between both
// directions — a hardware UART would need external direction control that is not there.
constexpr uint32_t kBitUs     = 104; // 1 / 9600 s, rounded
constexpr uint32_t kHalfBitUs = 52;

constexpr uint32_t kFirstByteTimeoutUs = 500000; // probe wake can be slow
constexpr uint32_t kInterByteTimeoutUs = 5000;   // gap that ends a frame
constexpr size_t   kRxCapacity         = 96;

// Header bytes between the delimiter and the first record: dest, source, sequence, type,
// length, payload type.
constexpr size_t kHeaderBytes = 6;

} // namespace

const char *battery_result_name(BatteryResult r)
{
    switch (r) {
    case BatteryResult::Ok:          return "ok";
    case BatteryResult::NoReply:     return "no reply";
    case BatteryResult::ShortFrame:  return "short frame";
    case BatteryResult::BadFrame:    return "bad frame";
    case BatteryResult::BadChecksum: return "bad checksum";
    case BatteryResult::NoRecords:   return "no records";
    }
    return "?";
}

void Battery::tx_byte(uint8_t b)
{
    // Interrupts off for the duration of the byte: at 104 us per bit, a single interrupt
    // landing mid-byte is enough to shift a bit boundary and corrupt the frame.
    noInterrupts();

    pinMode(m_pin, OUTPUT);
    digitalWrite(m_pin, LOW); // start bit
    delayMicroseconds(kBitUs);

    for (uint8_t i = 0; i < 8; i++) {
        digitalWrite(m_pin, (b & 0x01) ? HIGH : LOW);
        b >>= 1;
        delayMicroseconds(kBitUs);
    }

    digitalWrite(m_pin, HIGH); // stop bit
    delayMicroseconds(kBitUs);

    // Release to the pull-up so the probe can drive the shared line.
    pinMode(m_pin, INPUT_PULLUP);

    interrupts();
}

int Battery::rx_byte(uint32_t timeout_us)
{
    const uint32_t start = micros();
    while (digitalRead(m_pin) == HIGH) {
        if ((micros() - start) > timeout_us) {
            return -1;
        }
    }

    // Land in the middle of bit 0: half a bit to the centre of the start bit, then one
    // full bit forward. Sampling at the centre is what tolerates clock mismatch.
    delayMicroseconds(kHalfBitUs + kBitUs);

    uint8_t v = 0;
    for (uint8_t i = 0; i < 8; i++) {
        if (digitalRead(m_pin) == HIGH) {
            v |= (uint8_t)(1 << i);
        }
        delayMicroseconds(kBitUs);
    }

    delayMicroseconds(kBitUs); // stop bit
    return v;
}

void Battery::send_query()
{
    for (uint8_t i = 0; i < kWakeCount; i++) {
        tx_byte(kWakeByte);
    }

    const uint8_t body[] = {
        kDelimiter, kProbeId, kMasterId, 0x00, kTypeSendData, 0x01, kPayloadSendData,
    };

    // Checksum is the XOR of everything after the delimiter.
    uint8_t checksum = 0;
    for (size_t i = 1; i < sizeof(body); i++) {
        checksum ^= body[i];
    }

    for (size_t i = 0; i < sizeof(body); i++) {
        tx_byte(body[i]);
    }
    tx_byte(checksum);
}

size_t Battery::receive(uint8_t *buf, size_t cap)
{
    size_t n = 0;

    int v = rx_byte(kFirstByteTimeoutUs);
    if (v < 0) {
        return 0;
    }
    buf[n++] = (uint8_t)v;

    // Every subsequent byte gets a short window. A gap longer than that means the probe
    // has finished talking, which is what delimits the frame — there is no length field
    // we can trust before parsing.
    while (n < cap) {
        v = rx_byte(kInterByteTimeoutUs);
        if (v < 0) {
            break;
        }
        buf[n++] = (uint8_t)v;
    }
    return n;
}

BatteryResult Battery::parse(const uint8_t *buf, size_t len, BatteryReading &out)
{
    size_t start = 0;
    while (start < len && buf[start] != kDelimiter) {
        start++;
    }
    if (start >= len) {
        return BatteryResult::BadFrame;
    }

    // Everything after the delimiter, excluding the trailing checksum byte.
    const size_t body = start + 1;
    if (body + kHeaderBytes >= len) {
        return BatteryResult::BadFrame;
    }

    // The header's fifth byte is the declared payload length, counting the records that
    // follow the payload-type byte. Trusting it is what stops the record scan from running
    // off into whatever trailing noise the line picked up.
    const size_t declared    = buf[body + 4];
    const size_t records     = body + kHeaderBytes;
    const size_t records_end = records + declared;

    // The checksum sits immediately after the records. If the declared length disagrees
    // with how many bytes actually arrived, the frame is not trustworthy regardless of
    // what the records look like.
    if (records_end >= len) {
        return BatteryResult::BadFrame;
    }

    // Same convention as the request: XOR of every byte between the delimiter and the
    // checksum itself.
    //
    // ASSUMPTION, not yet confirmed against hardware: that the pack's reply uses the same
    // checksum convention as the request. The prior-art codec never checked the reply at
    // all, so there is nothing to copy here. If the first real pack reply is rejected,
    // compare the logged expected and received bytes before changing anything — the frame
    // may simply be framed differently. Tracked in issue #5.
    uint8_t expected = 0;
    for (size_t i = body; i < records_end; i++) {
        expected ^= buf[i];
    }

    if (expected != buf[records_end]) {
        LOGF("   battery : checksum %02X, expected %02X\n", buf[records_end], expected);
        return BatteryResult::BadChecksum;
    }

    // Records are { sensor id, IPSO type, value }, with the value width implied by the
    // type. An unknown type therefore has an unknown width, and there is no way to find
    // where the next record begins.
    //
    // Parsing stops there instead of guessing. Skipping a byte at a time would leave the
    // scan pointing into the middle of a value and read the tail of one record as the head
    // of another — producing a voltage or temperature that looks entirely plausible and is
    // simply wrong. A pack that reports a number nobody can challenge is worse than one
    // that reports nothing, so whatever was decoded before the unknown record is kept and
    // the rest is abandoned.
    size_t i = records;
    while (i + 2 < records_end) {
        const uint8_t type = buf[i + 1];

        if (type == kIpsoCapacity) {
            out.soc.set(buf[i + 2]);
            i += 3;
        } else if (type == kIpsoDcCurrent && (i + 3) < records_end) {
            out.current.set((int16_t)(((uint16_t)buf[i + 2] << 8) | buf[i + 3]));
            i += 4;
        } else if (type == kIpsoDcVoltage && (i + 3) < records_end) {
            out.voltage.set((uint16_t)(((uint16_t)buf[i + 2] << 8) | buf[i + 3]));
            i += 4;
        } else if (type == kIpsoTemperature && (i + 3) < records_end) {
            // Type 103 is the same code the pack's own LoRaWAN uplinks use for
            // temperature, carried as a signed 16-bit value.
            out.temperature.set((int16_t)(((uint16_t)buf[i + 2] << 8) | buf[i + 3]));
            i += 4;
        } else {
            // Worth logging: it means the pack sends something this build does not know
            // about, and every record behind it is being discarded.
            LOGF("   battery : unknown record type %u — stopping here\n", type);
            break;
        }
    }

    return out.any() ? BatteryResult::Ok : BatteryResult::NoRecords;
}

BatteryReading Battery::read()
{
    BatteryReading out;

    pinMode(m_pin, INPUT_PULLUP);
    delay(2);

    send_query();
    delay(2); // let the probe turn the line around

    uint8_t      rx[kRxCapacity];
    const size_t n = receive(rx, sizeof(rx));

    if (n == 0) {
        m_last = BatteryResult::NoReply;
    } else if (n < 8) {
        m_last = BatteryResult::ShortFrame;
    } else {
        m_last = parse(rx, n, out);
    }

    // A frame that arrived but did not verify is the one case worth dumping in full. It is
    // the difference between "the pack is not answering" and "the pack is answering in a
    // shape this parser does not expect", and those need completely different fixes.
    if (m_last == BatteryResult::BadFrame || m_last == BatteryResult::BadChecksum) {
        LOG(F("   battery : raw"));
        for (size_t i = 0; i < n; i++) {
            LOGF(" %02X", rx[i]);
        }
        LOGLN("");
    }

    // Leave the pin as a plain input. Holding the pull-up enabled costs current through
    // the pack's line resistor for the whole sleep interval.
    pinMode(m_pin, INPUT);

    if (m_last != BatteryResult::Ok) {
        LOGF("   battery : no data (%s, %u bytes)\n", battery_result_name(m_last),
             (unsigned)n);
        return out;
    }

    LOG(F("   battery : "));
    if (out.voltage.valid) {
        LOGF("%u.%02u V  ", out.voltage.value / 100, out.voltage.value % 100);
    }
    if (out.current.valid) {
        LOGF("%+d.%02u A  ", out.current.value / 100, abs(out.current.value % 100));
    }
    if (out.soc.valid) {
        LOGF("%u%%  ", out.soc.value);
    }
    if (out.temperature.valid) {
        LOGF("%d.%u C", out.temperature.value / 10, abs(out.temperature.value % 10));
    }
    LOGLN("");

    return out;
}

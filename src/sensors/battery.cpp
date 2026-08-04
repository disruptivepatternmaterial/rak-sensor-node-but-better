#include "battery.h"

#include "../build_features.h"

#include <Arduino.h>

namespace {

// The RAK Sensor Hub one-wire link is NOT a bare TLV stream. Every frame is a RUI3
// transport frame — { wakeup, delimiter, 16-bit length, type, flag, payload } — carrying a
// SensorHub API frame — { dest, source, sequence, hub-type, payload-length, payload-type,
// payload } — terminated by a one-byte checksum. Omitting the length/type/flag transport
// header (as the previous revision did) makes the pack reject the frame in
// verify_rui3type()/verify_checksum() and never reply — the 0-byte symptom.
//
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] beegee-tokyo/RAK-OneWireSerial
//   src/onewire_master_protocol.h — RUI3_Api_t{wakeup,start,length,type,flag,payload} and
//   SNHub_Api_t{dest,source,sequence,type,payload_length,payload_type,payload}; WAKEUPBYTE
//   0xFF, DELIMTER 0x7E, RUI3API_TYPE_SENSORHUB 2, RUI3API_FLG_REQ 0, SNHUB_TYPE_PROVISION
//   1, SNHUB_TYPE_SENDAT 3, PLD_PROVI_TYPE_BOOT 2, PLD_SDATA_TPYE_SENDAT 2.
// CITE(prior-art): [CIT-MESHTASTIC-9154] meshtastic/firmware @ 02050a4
//   variants/rak2560/RAK9154Sensor.cpp — the working nRF52840 reader. runOnce() calls
//   RakSNHub_Protocl_API.init(), which sends an SNHUB_TYPE_PROVISION/BOOT frame first, and
//   only then requests data (get.data) for the PID the pack announces. We replicate that
//   boot-then-request order.
constexpr uint8_t kWakeByte  = 0xFF; // RUI3_Api_t.wakeup
constexpr uint8_t kDelimiter = 0x7E; // RUI3_Api_t.start
constexpr uint8_t kWakeCount = 4;    // extra 0xFF settle the line; the pack scans for 0x7E

// CITE(prior-art): [CIT-MESHTASTIC-9154] probe addressing on the Sensor Hub bus.
constexpr uint8_t kProbeId     = 0x01; // dest of the single provisioned probe
constexpr uint8_t kMasterId    = 0x00; // PID_MASTER (source)
constexpr uint8_t kBroadcastId = 0xFF; // PID_UNKNOW — BOOT/provision is addressed here

// RUI3 transport header, between the delimiter and the SensorHub frame.
constexpr uint8_t kRui3TypeSensorHub = 0x02; // RUI3API_TYPE_SENSORHUB
constexpr uint8_t kRui3FlagReq       = 0x00; // RUI3API_FLG_REQ
// SHORT_SWAP(sizeof(SNHub_Api_t)=6) in wire order (low byte, then high byte). Both the
// BOOT and SENDAT requests carry a zero-length payload, so the length is 6 for both.
constexpr uint8_t kLenLo = 0x00;
constexpr uint8_t kLenHi = 0x06;

// SensorHub frame fields.
constexpr uint8_t kHubTypeProvision = 0x01; // SNHUB_TYPE_PROVISION
constexpr uint8_t kHubTypeSendData  = 0x03; // SNHUB_TYPE_SENDAT
constexpr uint8_t kPldBoot          = 0x02; // PLD_PROVI_TYPE_BOOT
constexpr uint8_t kPayloadSendData  = 0x02; // PLD_SDATA_TPYE_SENDAT

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

// SensorHub frame header (inside the RUI3 payload) before the first record: dest, source,
// sequence, hub-type, payload-length, payload-type.
constexpr size_t kHubHeaderBytes = 6;

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

void Battery::send_frame(uint8_t dest, uint8_t hub_type, uint8_t payload_type)
{
    // SensorHub frame: dest, source, sequence, hub-type, payload-length(0), payload-type.
    const uint8_t seq    = ++m_seq;
    const uint8_t hub[6] = {dest, kMasterId, seq, hub_type, 0x00, payload_type};

    // Checksum is NOT an XOR. It is the sum of the set-bit counts (popcount) of the RUI3
    // type byte, the RUI3 flag byte, and every byte the length field covers (the six
    // SensorHub bytes), accumulated into a uint8_t.
    // CITE(prior-art): [CIT-ONEWIRE-SERIAL] onewire_master_protocol.c cal_chksum():
    //   chsum = popcount(type) + popcount(flag) + sum popcount(payload[0..len-1]).
    uint8_t checksum = (uint8_t)(__builtin_popcount(kRui3TypeSensorHub) +
                                 __builtin_popcount(kRui3FlagReq));
    for (size_t i = 0; i < sizeof(hub); i++) {
        checksum += __builtin_popcount(hub[i]);
    }

    for (uint8_t i = 0; i < kWakeCount; i++) {
        tx_byte(kWakeByte);
    }
    tx_byte(kDelimiter);
    tx_byte(kLenLo);
    tx_byte(kLenHi);
    tx_byte(kRui3TypeSensorHub);
    tx_byte(kRui3FlagReq);
    for (size_t i = 0; i < sizeof(hub); i++) {
        tx_byte(hub[i]);
    }
    tx_byte(checksum);
}

// Broadcast BOOT/provision request. The reference driver sends this at init before any
// data request; an un-provisioned pack does not answer a SENDAT.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] onewire_master_protocol.c api_init() ->
//   snhub_provision_command(PID_UNKNOW, SNHUB_GS_SET, PLD_PROVI_TYPE_BOOT).
void Battery::send_boot()
{
    send_frame(kBroadcastId, kHubTypeProvision, kPldBoot);
}

// "Send your latest sensor data" to the provisioned probe.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] onewire_master_protocol.c snhub_snsrdat_command()
//   with SNHUB_TYPE_SENDAT + PLD_SDATA_TPYE_SENDAT, zero-length payload.
void Battery::send_query()
{
    send_frame(kProbeId, kHubTypeSendData, kPayloadSendData);
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
    size_t d = 0;
    while (d < len && buf[d] != kDelimiter) {
        d++;
    }
    if (d >= len) {
        return BatteryResult::BadFrame;
    }

    // RUI3 payload length (covers the SensorHub frame + records), from the 16-bit length
    // field just after the delimiter. LSB_COMB(hbyte, lbyte) = (lbyte << 8) + hbyte, with
    // lbyte at delimiter+1 and hbyte at delimiter+2.
    // CITE(prior-art): [CIT-ONEWIRE-SERIAL] onewire_master_protocol.c LSB_COMB +
    //   verify_checksum(): checksum = cal_chksum(); compare rui3->payload[payload_len].
    if (d + 4 >= len) {
        return BatteryResult::BadFrame;
    }
    const size_t payload_len = ((size_t)buf[d + 1] << 8) | buf[d + 2];

    const size_t payload   = d + 5; // after length[2] + rui3 type + rui3 flag
    const size_t cksum_idx = payload + payload_len;
    if (cksum_idx >= len) {
        return BatteryResult::BadFrame;
    }

    // Checksum: popcount(rui3 type) + popcount(rui3 flag) + popcount over every byte the
    // length field covers. Same algorithm as the request (cal_chksum).
    uint8_t expected = (uint8_t)(__builtin_popcount(buf[d + 3]) + __builtin_popcount(buf[d + 4]));
    for (size_t i = 0; i < payload_len; i++) {
        expected += __builtin_popcount(buf[payload + i]);
    }

    if (expected != buf[cksum_idx]) {
        LOGF("   battery : checksum %02X, expected %02X\n", buf[cksum_idx], expected);
        return BatteryResult::BadChecksum;
    }

    // Records occupy payload[6 .. payload_len-1], i.e. after the 6-byte SensorHub header.
    const size_t records     = payload + kHubHeaderBytes;
    const size_t records_end = cksum_idx;

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
    // Multi-byte values in the pack's SENDAT response are little-endian (LSB first).
    // CITE(prior-art): [CIT-MESHTASTIC-9154] RAK9154Sensor.cpp SNHUBAPI_EVT_SDATA_REQ path
    //   decodes dc_cur/dc_vol as (msg[2] << 8) + msg[1], i.e. value byte 0 is the low byte.
    //   (The unsolicited REPORT path uses the opposite order; a GET elicits SDATA_REQ.)
    size_t i = records;
    while (i + 2 < records_end) {
        const uint8_t type = buf[i + 1];

        if (type == kIpsoCapacity) {
            out.soc.set(buf[i + 2]);
            i += 3;
        } else if (type == kIpsoDcCurrent && (i + 3) < records_end) {
            out.current.set((int16_t)(((uint16_t)buf[i + 3] << 8) | buf[i + 2]));
            i += 4;
        } else if (type == kIpsoDcVoltage && (i + 3) < records_end) {
            out.voltage.set((uint16_t)(((uint16_t)buf[i + 3] << 8) | buf[i + 2]));
            i += 4;
        } else if (type == kIpsoTemperature && (i + 3) < records_end) {
            // Type 103 is the same code the pack's own LoRaWAN uplinks use for temperature,
            // a signed 16-bit value. ASSUMED little-endian to match the current/voltage
            // records above; the Meshtastic reader does not decode this type, so confirm
            // the byte order against a real reply before trusting the sign.
            out.temperature.set((int16_t)(((uint16_t)buf[i + 3] << 8) | buf[i + 2]));
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

    // Phase 1: BOOT/provision handshake. The reference driver always does this first (via
    // RakSNHub_Protocl_API.init); the pack replies with its provision record, which is what
    // enumerates the probe. We ignore the reply's contents and address the probe as 0x01,
    // exactly as the single-probe Meshtastic reader does.
    // CITE(prior-art): [CIT-MESHTASTIC-9154] RAK9154Sensor::runOnce -> API.init() ->
    //   provision BOOT, then get.data for the announced PID.
    send_boot();
    delay(2); // let the probe turn the line around
    uint8_t provision[kRxCapacity];
    (void)receive(provision, sizeof(provision));
    delay(2);

    // Phase 2: request the latest sensor data.
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

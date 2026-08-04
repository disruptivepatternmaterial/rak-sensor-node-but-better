#include "battery.h"

#include "../build_features.h"

#include <Arduino.h>
#include <SoftwareHalfSerial.h>

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

// Probe addressing. kProbeId is the id the master *assigns*, not an id the pack has before
// being asked: `pid = aid + 1` for the first free record slot, so the first probe on the bus
// becomes 0x01. Until that assignment lands the pack is still 0xFF, and addressing it as
// 0x01 is exactly the production bug this revision fixes.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.h PID_UNKNOW 0xFF,
//   PID_MASTER 0x00; onewire_master_protocol.c snhub_provision_req_program() derives
//   `U8 pid = aid + 1; hub_api_prov->provId = pid;` and only then emits EVT_ADD_PID.
// CITE(prior-art): [CIT-MESHTASTIC-9154] @ 02050a4 RAK9154Sensor.cpp — `provision = msg[0]`
//   on SNHUBAPI_EVT_ADD_PID, then `get.data(provision)`. It queries 0x01 because the
//   handshake already assigned 0x01, not because 0x01 is a fixed address.
// CITE(bench): docs/EVIDENCE.md — SENDAT dest sweep at 9600 on commit 3d3425d: dest 0x01,
//   0x02 and 0x03 each returned 0 bytes; dest 0xFF returned a full 28-byte SENDAT response.
//   The pack listens on 0xFF because nothing had ever provisioned it.
constexpr uint8_t kProbeId     = 0x01; // first assignable PID (record index 0 -> pid 1)
constexpr uint8_t kMasterId    = 0x00; // PID_MASTER (source)
constexpr uint8_t kBroadcastId = 0xFF; // PID_UNKNOW — where an un-provisioned pack listens

// RUI3 transport header, between the delimiter and the SensorHub frame.
constexpr uint8_t kRui3TypeSensorHub = 0x02; // RUI3API_TYPE_SENSORHUB
constexpr uint8_t kRui3FlagReq       = 0x00; // RUI3API_FLG_REQ
constexpr uint8_t kRui3FlagRsp       = 0x01; // RUI3API_FLG_RSP
// SHORT_SWAP(sizeof(SNHub_Api_t)=6) in wire order (low byte, then high byte). Both the
// BOOT and SENDAT requests carry a zero-length payload, so the length is 6 for both.
constexpr uint8_t kLenLo = 0x00;
constexpr uint8_t kLenHi = 0x06;

// SensorHub frame fields.
constexpr uint8_t kHubTypeProvision = 0x01; // SNHUB_TYPE_PROVISION
constexpr uint8_t kHubTypeSendData  = 0x03; // SNHUB_TYPE_SENDAT
constexpr uint8_t kPldBoot          = 0x02; // PLD_PROVI_TYPE_BOOT
constexpr uint8_t kPayloadSendData  = 0x02; // PLD_SDATA_TPYE_SENDAT

// The pack's self-announcement is a PROVISION request of payload type VER3 — and VER3 is
// the *only* provision payload type the reference master accepts. VER1, VER2 and BOOT all
// fall through to `return RET_ERROR`, so a driver that expects to be answered in one of
// those shapes will wait forever.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.h
//   PLD_PROVI_TYPE_E { VER1=0, VER2=1, BOOT=2, VER3=3 }; onewire_master_protocol.c
//   snhub_provision_req_program() switches on payload_type and bypasses only VER3.
// CITE(bench): docs/EVIDENCE.md — passive listen at 9600 on commit 3d3425d, no bytes
//   transmitted: `FF 7E 00 55 02 00 | 00 FF 00 01 50 03 | ...`. hub_type 0x01 PROVISION,
//   payload_type 0x03 = VER3, dest 0x00 master, source 0xFF unprovisioned.
constexpr uint8_t kPldProvVer3 = 0x03; // PLD_PROVI_TYPE_VER3

// Byte offset of `provId` inside SNHub_Api_Provision_t: hw_version(1) + sw_version(3) +
// sn(18) = 22. Confirmed against the captured announcement, where model_name — the field
// this offset positions everything else against — lands exactly where the struct says it
// should: offset 30, which is frame index 42, which is where "RAK2560-io" begins.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.h
//   SNHub_Api_Provision_t is ATT_PACKED { hw_version, sw_version[3], SERIALNUM sn (18),
//   provId, reserved1[7], MDLNAME model_name (20), reserved2[4], snsr_num, snsr_type[] }.
// CITE(bench): docs/EVIDENCE.md — announcement captured on 3d3425d carries 0xFF at frame
//   index 34 (= payload + 6 + 22, the provId slot) and `52 41 4B 32 35 36 30 2D 69 6F`
//   ("RAK2560-io") at index 42, which is offset 30. The layout is not inferred, it is seen.
constexpr size_t kProvIdOffset = 22;

// CITE(prior-art): [CIT-ONEWIRE-SERIAL] IPSO codes, already reduced by the 3200 offset.
//   These are the same numbers the payload encoder uses on the LoRaWAN side, because RAK
//   reuses the IPSO table in both places — which is a convenience, not a coincidence.
constexpr uint8_t kIpsoTemperature = 103; // 3303 - 3200
constexpr uint8_t kIpsoCapacity    = 184; // 3384 - 3200
constexpr uint8_t kIpsoDcCurrent   = 185; // 3385 - 3200
constexpr uint8_t kIpsoDcVoltage   = 186; // 3386 - 3200

// 8N1 on a single open-drain wire shared between both directions — a hardware UART would
// need external direction control that is not there, so the bit timing is done in software.
//
// It is NOT done here, though, and that distinction is the whole point of this revision.
// The previous implementation drove the line with digitalWrite() and delayMicroseconds(104)
// per bit. On the nRF52840 an Arduino digitalWrite() is a function call that resolves the
// pin through the variant table and then does a read-modify-write on the port register;
// that cost lands *on top of* every delay, so each bit period overshoots 104 us and the
// error accumulates across the ten bits of a byte. By the stop bit the frame is skewed far
// enough that the pack discards it — which is exactly the observed symptom: a correct frame
// (375e99a fixed the content) that still draws zero bytes in reply.
//
// The reference implementation avoids that by caching the port output register and the pin
// bit mask once, then setting each bit with a single `*reg |= mask` / `*reg &= ~mask`. The
// write is a couple of instructions instead of a function call, so delayMicroseconds()
// dominates the bit period and 9600 comes out accurate. RX is not polled at all: a GPIOTE
// falling-edge event fires on the start bit, and the handler centres the first sample at
// half a bit minus 2 us to pay for interrupt latency, then walks the byte at one bit minus
// 1 us with sixteen NOPs of padding to trim the residual.
//
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 src/SoftwareHalfSerial.cpp — write():
//   `volatile uint32_t *reg = _transmitPortRegister; ... *reg |= reg_mask` per bit, with
//   only NRF_GPIOTE->INTENCLR masked (not global interrupts, which the SoftDevice forbids
//   holding); setTX()/setRX() cache portOutputRegister(digitalPinToPort(pin)) and
//   digitalPinToBitMask(pin); recv() is the GPIOTE handler with _rx_delay_centering =
//   bit/2 - 2, _rx_delay_intrabit = bit - 1, and the 16-NOP block.
// CITE(datasheet): [CIT-NRF-GPIO] nRF52840 Product Specification, GPIO — the OUT/OUTSET/
//   OUTCLR port registers are what a pin write ultimately touches; writing them directly is
//   what removes the per-bit software overhead the Arduino wrapper adds.
// CITE(datasheet): [CIT-NRF-GPIOTE] nRF52840 Product Specification, GPIOTE — IN events with
//   CONFIG.POLARITY = HiToLo give a hardware edge-detect on the start bit, which is how RX
//   starts on time instead of inside a polling loop that may already be a bit late.
// CITE(prior-art): [CIT-MESHTASTIC-9154] @ 02050a4 variants/rak2560/RAK9154Sensor.cpp —
//   drives this same library on an nRF52840: `static SoftwareHalfSerial mySerial(pin);`
//   mySerial.begin(9600); then write() on send and available()/read() on receive.
constexpr long kBaud = 9600;

// One wire, one bus. The library holds its RX ring buffer and `active_object` in class
// statics, so it is a singleton by construction; binding it to the pin on first use keeps
// the pin in one place (main.cpp) and keeps this nRF-only type out of battery.h.
SoftwareHalfSerial &bus(uint8_t pin)
{
    static SoftwareHalfSerial instance(pin);
    return instance;
}

constexpr uint32_t kFirstByteTimeoutUs = 500000; // probe wake can be slow
constexpr uint32_t kInterByteTimeoutUs = 5000;   // gap that ends a frame
constexpr size_t   kRxCapacity         = 96;

// SensorHub frame header (inside the RUI3 payload) before the first record: dest, source,
// sequence, hub-type, payload-length, payload-type.
constexpr size_t kHubHeaderBytes = 6;

// The provisioning announcement is far larger than a data reply: the captured one declares
// a payload length of 85, so the frame runs 92 bytes, and it grows with the probe's sensor
// count. It also has to be answered by echoing it back, so it must be buffered whole — a
// truncated announcement cannot be turned around.
// CITE(bench): docs/EVIDENCE.md — announcement on 3d3425d declares length 0x0055 = 85,
//   giving delimiter + 5 header + 85 + 1 checksum = 92 bytes from the wake byte.
constexpr size_t kProvCapacity = 160;

// cal_chksum() over a frame already in a buffer: popcount of the RUI3 type byte, plus the
// RUI3 flag byte, plus every byte the length field covers. Shared by the verify path and the
// provisioning response so the two can never disagree about the algorithm.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.c cal_chksum().
uint8_t frame_chksum(const uint8_t *buf, size_t delim, size_t payload_len)
{
    uint8_t c =
        (uint8_t)(__builtin_popcount(buf[delim + 3]) + __builtin_popcount(buf[delim + 4]));
    for (size_t i = 0; i < payload_len; i++) {
        c += __builtin_popcount(buf[delim + 5 + i]);
    }
    return c;
}

// A located, checksum-verified frame inside a receive buffer.
struct SnHubFrame {
    size_t  delim;       // index of the 0x7E
    size_t  payload;     // index of the first SensorHub byte
    size_t  payload_len; // RUI3 length field — covers the SensorHub frame and its records
    size_t  cksum;       // index of the trailing checksum byte
    uint8_t flag;        // RUI3 flag: request or response
    uint8_t dest;
    uint8_t source;
    uint8_t hub_type;
    uint8_t hub_payload_type;
};

// Find the next verified frame at or after `from`.
//
// Scanning rather than assuming the buffer starts with one frame is not defensive padding —
// it is required. The bench capture shows a SENDAT reply and a provisioning announcement
// arriving back to back inside a single 64-byte read, so "the first delimiter in the buffer"
// is routinely the wrong frame. The previous parser took the first delimiter unconditionally
// and would have walked an 80-byte provisioning payload as if it were IPSO records.
//
// The checksum is verified here rather than by the caller so that a 0x7E occurring *inside*
// a payload cannot be mistaken for a frame start: a false start produces a false length and
// therefore a failed checksum, and the scan simply moves on to the next candidate byte.
//
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.c api_process():
//   verify_delimter() locates 0x7E, then verify_checksum() -> verify_rui3type() gate the
//   frame before any program runs. Same order here.
// CITE(bench): docs/EVIDENCE.md — SENDAT sweep on 3d3425d returned one buffer holding both
//   `FF 7E 00 15 02 01 ...` (the 28-byte data reply) and `FF 7E 00 55 02 00 ...` (the
//   announcement) concatenated, which is what makes the scan mandatory.
bool next_frame(const uint8_t *buf, size_t len, size_t from, SnHubFrame &f, bool &bad_cksum)
{
    for (size_t d = from; d < len; d++) {
        if (buf[d] != kDelimiter) {
            continue;
        }
        // length[2] + type + flag, and at least the SensorHub header behind it.
        if (d + 5 + kHubHeaderBytes > len) {
            break; // nothing this short can hold a frame, and later candidates are shorter
        }
        if (buf[d + 3] != kRui3TypeSensorHub) {
            continue;
        }

        // LSB_COMB(hbyte, lbyte) = (lbyte << 8) + hbyte, with lbyte first on the wire.
        const size_t payload_len = ((size_t)buf[d + 1] << 8) | buf[d + 2];
        if (payload_len < kHubHeaderBytes) {
            continue;
        }
        const size_t payload = d + 5;
        const size_t cksum   = payload + payload_len;
        if (cksum >= len) {
            continue; // declared longer than what arrived
        }
        if (frame_chksum(buf, d, payload_len) != buf[cksum]) {
            bad_cksum = true;
            continue;
        }

        f.delim            = d;
        f.payload          = payload;
        f.payload_len      = payload_len;
        f.cksum            = cksum;
        f.flag             = buf[d + 4];
        f.dest             = buf[payload + 0];
        f.source           = buf[payload + 1];
        f.hub_type         = buf[payload + 3];
        f.hub_payload_type = buf[payload + 5];
        return true;
    }
    return false;
}

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
    case BatteryResult::Unsampled:   return "all-zero records (pack not sampled)";
    }
    return "?";
}

// One byte out. The library turns the line around for us: write() detaches the RX edge
// interrupt, drives the pin as an output for the ten bit periods, then puts it back to
// input-with-pull-up and re-attaches — so the probe can answer the moment we stop talking,
// and our own start bit never re-triggers our own receiver.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 SoftwareHalfSerial::write() ->
//   beginTx()/beginRx(); this per-byte turnaround is the same path Meshtastic's
//   SNHUBAPI_EVT_QSEND takes when it hands a whole frame to mySerial.write(msg, len).
void Battery::tx_byte(uint8_t b)
{
    bus(m_pin).write(b);
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

// Broadcast BOOT/provision request, as the reference sends at init and on reboot.
//
// BOOT is a nudge, not a question. The bench sweep showed it drawing 0 bytes at every baud,
// and that is not a failure: nothing in the reference master expects a reply to it. Its
// effect is to make probes re-announce themselves, and the announcement then arrives as an
// independent PROVISION *request* addressed to the master. The pack in fact announces itself
// spontaneously and repeatedly whether or not BOOT is sent, so this is belt and braces —
// worth keeping, because a pack that has gone quiet is the case where it matters.
//
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.c api_init() and
//   api_set_provision() both call snhub_provision_command(PID_UNKNOW, 0, SNHUB_GS_SET,
//   PLD_PROVI_TYPE_BOOT) with a zero-length payload, and no code path awaits a response.
// CITE(bench): docs/EVIDENCE.md — BOOT broadcast on 3d3425d returned 0 bytes at 4800, 9600,
//   19200 and 38400, while a passive listen at 9600 that transmitted nothing at all still
//   received the announcement every cycle. The pack initiates; the master answers.
void Battery::send_boot()
{
    send_frame(kBroadcastId, kHubTypeProvision, kPldBoot);
}

// "Send your latest sensor data", then collect the reply.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.c
//   snhub_snsrdat_command() with SNHUB_TYPE_SENDAT + PLD_SDATA_TPYE_SENDAT.
size_t Battery::query(uint8_t dest, uint8_t *buf, size_t cap)
{
    bus(m_pin).flush(); // anything still queued predates this request
    send_frame(dest, kHubTypeSendData, kPayloadSendData);
    delay(2); // let the probe turn the line around
    return receive(buf, cap);
}

// Answer the pack's provisioning announcement.
//
// This is the piece that was missing, and the reason the pack reported nothing but zeros.
// The master does not poll for provisioning — it *replies* to it. The pack sends a PROVISION
// request (flag = REQ) addressed to the master with `source = 0xFF` and `provId = 0xFF`,
// meaning "I have no id". The master's entire job is to send that same frame back with the
// request flag turned into a response flag, the addresses swapped, and `provId` filled in
// with the id it is assigning. Everything else in the frame — including the probe's own
// sequence number — is echoed byte for byte.
//
// So the response is built by mutating the received frame in place rather than composing a
// new one. That is not a shortcut: the announcement carries 80 bytes of serial number, model
// name and per-sensor descriptors that the master neither parses nor is able to regenerate,
// and the checksum covers all of them.
//
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.c
//   snhub_provision_req_program(): f_memcpy(pktBuff, data, len) then
//   `rui3_api->flag = RUI3API_FLG_RSP; hub_api->dest = source; hub_api->source = dest;
//   hub_api_prov->provId = pid;` recomputes cal_chksum() into payload[recv_len] and sends
//   the buffer back via SNHUBAPI_EVT_QSEND at the original length. The sequence is untouched.
// CITE(prior-art): [CIT-MESHTASTIC-9154] @ 02050a4 RAK9154Sensor.cpp — the QSEND that
//   carries this response goes straight to mySerial.write(msg, len); the ADD_PID event that
//   follows is what tells the consumer which id to query from then on.
// CITE(bench): docs/EVIDENCE.md — the announcement captured on 3d3425d carries provId 0xFF
//   at frame index 34 and re-announces every cycle with an incrementing sequence, i.e. the
//   pack was still waiting to be assigned an id after every previous firmware revision.
bool Battery::provision(uint8_t *buf, size_t len)
{
    SnHubFrame f;
    bool       bad_cksum = false;
    size_t     from      = 0;

    while (next_frame(buf, len, from, f, bad_cksum)) {
        from = f.delim + 1;

        if (f.hub_type != kHubTypeProvision || f.flag != kRui3FlagReq) {
            continue;
        }
        // VER3 is the only provision payload type the reference master will act on.
        if (f.hub_payload_type != kPldProvVer3) {
            continue;
        }
        if (f.dest != kMasterId) {
            continue; // somebody else's business
        }

        const size_t prov = f.payload + kHubHeaderBytes;
        if (prov + kProvIdOffset >= f.cksum) {
            continue; // too short to hold a provId — not a frame we can answer
        }

        buf[f.delim + 4]          = kRui3FlagRsp; // request becomes response
        buf[f.payload + 0]        = f.source;     // dest := the probe that announced
        buf[f.payload + 1]        = kMasterId;    // source := us
        buf[prov + kProvIdOffset] = kProbeId;     // the id we are assigning
        buf[f.cksum]              = frame_chksum(buf, f.delim, f.payload_len);

        for (uint8_t i = 0; i < kWakeCount; i++) {
            tx_byte(kWakeByte);
        }
        for (size_t i = f.delim; i <= f.cksum; i++) {
            tx_byte(buf[i]);
        }

        LOGF("   battery : provisioned probe 0x%02X as pid 0x%02X\n", f.source, kProbeId);
        return true;
    }
    return false;
}

// Drain whatever the GPIOTE receiver has buffered. Bytes are assembled in the interrupt
// handler and queued, so this no longer has to be sitting on the pin when the start bit
// arrives — the previous polling loop could miss a reply simply by being one bit late.
//
// CITE(prior-art): [CIT-MESHTASTIC-9154] @ 02050a4 RAK9154Sensor.cpp onewireHandle():
//   `while (mySerial.available()) { buff[bufflen++] = mySerial.read(); delay(2); }` — a
//   per-byte grace period is what delimits the frame, because there is no length field we
//   can trust before parsing. Same shape here, with the gap expressed as a timeout.
size_t Battery::receive(uint8_t *buf, size_t cap)
{
    SoftwareHalfSerial &link = bus(m_pin);

    // The first byte gets the long window: the probe may still be waking.
    uint32_t deadline_us = kFirstByteTimeoutUs;
    uint32_t mark        = micros();
    size_t   n           = 0;

    while (n < cap) {
        const int v = link.read();
        if (v >= 0) {
            buf[n++] = (uint8_t)v;
            // Every subsequent byte gets a short window. A gap longer than that means the
            // probe has finished talking, which is what delimits the frame.
            deadline_us = kInterByteTimeoutUs;
            mark        = micros();
            continue;
        }
        if ((micros() - mark) > deadline_us) {
            break;
        }
    }
    return n;
}

BatteryResult Battery::parse(const uint8_t *buf, size_t len, BatteryReading &out)
{
    // Pick the SENDAT frame out of the buffer. There may be more than one frame in there,
    // and the data reply is not reliably the first — see next_frame().
    SnHubFrame f;
    bool       bad_cksum = false;
    bool       found     = false;
    size_t     from      = 0;

    while (next_frame(buf, len, from, f, bad_cksum)) {
        from = f.delim + 1;
        if (f.hub_type == kHubTypeSendData) {
            found = true;
            break;
        }
    }
    if (!found) {
        return bad_cksum ? BatteryResult::BadChecksum : BatteryResult::BadFrame;
    }

    // The same records arrive in opposite byte orders depending on which way the exchange
    // went, and the reference driver decodes them differently in each case. A response to
    // our GET (flag = RSP) is little-endian; a push the pack sends unbidden (flag = REQ) is
    // big-endian. Getting this from the flag rather than assuming one of them is what stops
    // a 12.80 V pack being reported as 0.05 V.
    // CITE(prior-art): [CIT-MESHTASTIC-9154] @ 02050a4 RAK9154Sensor.cpp — the
    //   SNHUBAPI_EVT_SDATA_REQ case (reached via protocol_list[SENDAT].rsp) decodes
    //   `(msg[2] << 8) + msg[1]`, while SNHUBAPI_EVT_REPORT (reached via .req) decodes
    //   `(msg[1] << 8) + msg[2]`. msg[0] is the IPSO type, so msg[1] is the first value byte.
    // CITE(bench): docs/EVIDENCE.md — the reply captured on 3d3425d carries flag 0x01 (RSP),
    //   so a solicited read takes the little-endian path. Its values are all zero, which is
    //   why the byte order could not be settled from the capture alone and had to come from
    //   the reference.
    const bool lsb_first = (f.flag == kRui3FlagRsp);

    auto val16 = [&](size_t at) -> uint16_t {
        return lsb_first ? (uint16_t)(((uint16_t)buf[at + 1] << 8) | buf[at])
                         : (uint16_t)(((uint16_t)buf[at] << 8) | buf[at + 1]);
    };

    // Records occupy payload[6 ..], i.e. everything after the 6-byte SensorHub header up to
    // the checksum byte.
    const size_t records     = f.payload + kHubHeaderBytes;
    const size_t records_end = f.cksum;

    // Records are { sensor id, IPSO type, value }, the value width implied by the type. The
    // widths are not guessed — they are the `size` column of the reference's IPSO table, and
    // they check out exactly against the captured reply: four records in fifteen bytes as
    // 4 + 4 + 3 + 4, ending precisely on the checksum.
    //
    // An unknown type therefore has an unknown width, and there is no way to find where the
    // next record begins. Parsing stops there instead of guessing. Skipping a byte at a time
    // would leave the scan pointing into the middle of a value and read the tail of one
    // record as the head of another — producing a voltage or temperature that looks entirely
    // plausible and is simply wrong. A pack that reports a number nobody can challenge is
    // worse than one that reports nothing, so whatever was decoded before the unknown record
    // is kept and the rest is abandoned.
    //
    // CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.h
    //   SNHub_Api_sData_Snsr_t { sid, ipso, value[] }; onewire_master_protocol.c walks it as
    //   `sData = &sData->value[rakipso_tbl[sData->ipso].size]` with table sizes CAPACITY 1,
    //   DC_CURRENT 2, DC_VOLTAGE 2, TEMP_SENSOR 2.
    // CITE(bench): docs/EVIDENCE.md — SENDAT reply on 3d3425d: `15 BA 00 00 | 16 B9 00 00 |
    //   17 B8 00 | 18 67 00 00` then checksum 0x2F. Sensor ids 0x15-0x18 lead each record,
    //   the IPSO type follows, and the strides land on the checksum byte with nothing left
    //   over — the walker below is verified against real bytes, not inferred.
    size_t i = records;
    while (i + 2 < records_end) {
        const uint8_t type = buf[i + 1];

        if (type == kIpsoCapacity) {
            out.soc.set(buf[i + 2]);
            i += 3;
        } else if (type == kIpsoDcCurrent && (i + 3) < records_end) {
            out.current.set((int16_t)val16(i + 2));
            i += 4;
        } else if (type == kIpsoDcVoltage && (i + 3) < records_end) {
            out.voltage.set(val16(i + 2));
            i += 4;
        } else if (type == kIpsoTemperature && (i + 3) < records_end) {
            // Type 103 is the same code the pack's own LoRaWAN uplinks use for temperature,
            // a signed 16-bit value. Byte order follows the flag like the others; the
            // Meshtastic reader does not decode this type, so the sign is still unconfirmed
            // against a non-zero reading.
            out.temperature.set((int16_t)val16(i + 2));
            i += 4;
        } else {
            // Worth logging: it means the pack sends something this build does not know
            // about, and every record behind it is being discarded.
            LOGF("   battery : unknown record type %u — stopping here\n", type);
            break;
        }
    }

    if (!out.any()) {
        return BatteryResult::NoRecords;
    }

    // A verified frame full of zeros is not a measurement, and this is the one place that
    // distinction can still be made before the number becomes an uplink.
    //
    // The pack is powered by the cell it is reporting on. It cannot be at 0.00 V and also be
    // driving this wire, so a zero voltage means "I have no sample", not "the battery is
    // flat". Encoding it would put 0.00 V into a payload that the decoder cannot tell apart
    // from a real reading, and the operator would read it as a dead pack — which is the exact
    // failure the repo's null policy exists to prevent. So the whole record set is discarded
    // rather than partially trusted: if the voltage is a placeholder, the current, charge and
    // temperature beside it are placeholders too.
    //
    // Only voltage is used as the sentinel. 0 A is a genuine idle current, 0 % is a genuine
    // (alarming) state of charge, and 0.0 degC is an entirely ordinary temperature for a node
    // in the woods — nulling those on their own value would throw away real data.
    //
    // CITE(bench): docs/EVIDENCE.md — the SENDAT reply captured on 3d3425d verified its
    //   checksum and decoded cleanly to voltage 0, current 0, capacity 0, temperature 0 while
    //   the pack was demonstrably alive and re-announcing itself unprovisioned.
    if (out.voltage.valid && out.voltage.value == 0) {
        out = BatteryReading{};
        return BatteryResult::Unsampled;
    }

    return BatteryResult::Ok;
}

BatteryReading Battery::read()
{
    BatteryReading out;

    // begin() caches the port registers, arms the GPIOTE falling-edge interrupt, and leaves
    // the pin as input-with-pull-up so the idle line reads high. Everything after this point
    // is timing-critical only inside the library.
    SoftwareHalfSerial &link = bus(m_pin);
    link.begin(kBaud);
    link.flush();

    // Phase 1: complete the provisioning handshake.
    //
    // The previous revision had the direction of this exchange backwards. It sent BOOT and
    // waited to be answered, then threw the reply away and addressed the probe as 0x01
    // regardless. But BOOT is not a question — the pack is the initiator here. It announces
    // itself to the master with a PROVISION request carrying provId = 0xFF, and the master
    // completes the handshake by answering that announcement with the id it is assigning.
    // Nothing had ever answered, so the pack stayed unprovisioned, kept re-announcing, and
    // reported placeholder zeros.
    //
    // BOOT is still sent first, because its documented effect is to make probes re-announce
    // and that is precisely what this phase needs to hear.
    //
    // CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.c — the master
    //   has no provision *poll*: protocol_list[SNHUB_TYPE_PROVISION] defines only `.req`
    //   (snhub_provision_req_program, which handles an incoming announcement) and leaves
    //   `.rsp` NULL. Provisioning is inbound-driven by construction.
    // CITE(prior-art): [CIT-MESHTASTIC-9154] @ 02050a4 RAK9154Sensor.cpp — provisioning is
    //   never requested; `provision` is set only from SNHUBAPI_EVT_ADD_PID, which the library
    //   raises at the end of handling an inbound announcement, and get.data() follows on the
    //   next 50 ms scheduler tick.
    send_boot();
    delay(2); // let the probe turn the line around
    uint8_t      hello[kProvCapacity];
    const size_t hn = receive(hello, sizeof(hello));
    if (hn > 0 && provision(hello, hn)) {
        m_pid = kProbeId;
        delay(50); // the reference's tick interval — the only timing guidance available
    }
    link.flush(); // anything still queued from phase 1 is not part of the data reply

    // Phase 2: request the latest sensor data from the address the pack answers on.
    //
    // Two addresses are possible and which one is live depends on whether the handshake above
    // took effect: the assigned probe id once provisioned, 0xFF while not. Rather than assume,
    // try the preferred one and fall back, then remember whichever answered. The bench proved
    // a wrong choice here costs the entire reading — a SENDAT to 0x01 on an unprovisioned pack
    // draws total silence, which is indistinguishable from an unplugged cable.
    //
    // CITE(bench): docs/EVIDENCE.md — dest sweep on 3d3425d: 0x01/0x02/0x03 -> 0 bytes,
    //   0xFF -> a full 28-byte SENDAT response with a valid checksum.
    uint8_t       rx[kRxCapacity];
    const uint8_t fallback = (m_pid == kBroadcastId) ? kProbeId : kBroadcastId;

    size_t n = query(m_pid, rx, sizeof(rx));
    if (n == 0) {
        n = query(fallback, rx, sizeof(rx));
        if (n > 0) {
            m_pid = fallback;
        }
    }

    if (n == 0) {
        m_last = BatteryResult::NoReply;
    } else if (n < 8) {
        m_last = BatteryResult::ShortFrame;
    } else {
        m_last = parse(rx, n, out);
    }

    // A frame that arrived but did not yield a reading is the case worth dumping in full. It
    // is the difference between "the pack is not answering", "the pack is answering in a shape
    // this parser does not expect", and "the pack is answering with placeholders" — and those
    // need completely different fixes. Unsampled is included deliberately: the raw bytes are
    // the evidence for whether the zeros are the pack's or ours.
    if (m_last == BatteryResult::BadFrame || m_last == BatteryResult::BadChecksum ||
        m_last == BatteryResult::Unsampled) {
        LOG(F("   battery : raw"));
        for (size_t i = 0; i < n; i++) {
            LOGF(" %02X", rx[i]);
        }
        LOGLN("");
    }

    // Release the GPIOTE channel and the edge interrupt, then leave the pin as a plain
    // input. end() alone is not enough: the library idles RX with the pull-up enabled, and
    // holding that costs current through the pack's line resistor for the whole sleep
    // interval — which on a node that runs unattended for months is not a rounding error.
    // CITE(datasheet): [CIT-NRF-GPIO] nRF52840 PS, GPIO — PIN_CNF[n].PULL selects the pin's
    //   pull resistor independently of DIR, so re-running pinMode(INPUT) after end() is what
    //   actually removes it.
    link.end();
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

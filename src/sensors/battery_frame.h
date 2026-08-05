/*
 * RAK9154 one-wire frame codec — the Arduino-free half of the battery driver.
 *
 * Split out from battery.cpp for the same reason crc16.cpp is split out of modbus.cpp: this
 * is pure byte decoding, it has no dependency on the Arduino runtime, and it is therefore
 * testable on a normal computer in milliseconds. battery.cpp keeps the I/O and the timing —
 * the half that genuinely needs a board.
 *
 * The split is not cosmetic. The defect that cost the most bench time on this driver was a
 * 28-of-92-byte truncated announcement being classified as an all-zero record set: pure
 * classification logic, reachable from a fixed byte array, and it survived for days because
 * nothing could exercise it without a pack on the wire. See issue #42.
 *
 * CITE(prior-art): [CIT-ONEWIRE-SERIAL] beegee-tokyo/RAK-OneWireSerial @ c58c0f0
 *   onewire_master_protocol.{h,c} — RUI3_Api_t / SNHub_Api_t framing, cal_chksum(), and the
 *   verify_delimter -> verify_checksum -> verify_rui3type validation order followed here.
 * CITE(prior-art): [CIT-MESHTASTIC-9154] meshtastic/firmware @ 02050a4
 *   variants/rak2560/RAK9154Sensor.cpp — the record decode and its flag-dependent byte order.
 * CITE(sibling): [CIT-RAK45WIRE] rak-4-5-wire/firmware/nanoc6-onewire-poll @ efc0e3c
 *   onewire_master_protocol.c — the clean-room codec this framing follows.
 */

#pragma once

#include "battery.h"

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------
// Protocol constants shared by the codec and the transport.
//
// These live here rather than in battery.cpp because both halves of the split need them and
// a drifted copy in either one is a whole bench session. The transport-only constants (wake
// byte count, baud, timeouts) stay in battery.cpp, where the hardware is.
// ---------------------------------------------------------------------------------------

// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.h — DELIMTER 0x7E
//   is RUI3_Api_t.start, the byte every frame begins with behind the wake run.
constexpr uint8_t kDelimiter = 0x7E;

// RUI3 transport header, between the delimiter and the SensorHub frame.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.h —
//   RUI3API_TYPE_SENSORHUB 2, RUI3API_FLG_REQ 0, RUI3API_FLG_RSP 1.
constexpr uint8_t kRui3TypeSensorHub = 0x02; // RUI3API_TYPE_SENSORHUB
constexpr uint8_t kRui3FlagReq       = 0x00; // RUI3API_FLG_REQ
constexpr uint8_t kRui3FlagRsp       = 0x01; // RUI3API_FLG_RSP

// SensorHub frame fields.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.h —
//   SNHUB_TYPE_PROVISION 1, SNHUB_TYPE_SENDAT 3, PLD_PROVI_TYPE_VER3 3.
constexpr uint8_t kHubTypeProvision = 0x01; // SNHUB_TYPE_PROVISION
constexpr uint8_t kHubTypeSendData  = 0x03; // SNHUB_TYPE_SENDAT
constexpr uint8_t kPldProvVer3      = 0x03; // PLD_PROVI_TYPE_VER3

// PID_MASTER — the address this firmware answers on, and therefore the destination every
// genuine reply to one of our queries must carry.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.h PID_MASTER 0x00.
constexpr uint8_t kMasterId = 0x00;

// SensorHub frame header (inside the RUI3 payload) before the first record: dest, source,
// sequence, hub-type, payload-length, payload-type.
constexpr size_t kHubHeaderBytes = 6;

// CITE(prior-art): [CIT-ONEWIRE-SERIAL] IPSO codes, already reduced by the 3200 offset.
//   These are the same numbers the payload encoder uses on the LoRaWAN side, because RAK
//   reuses the IPSO table in both places — which is a convenience, not a coincidence.
constexpr uint8_t kIpsoTemperature = 103; // 3303 - 3200
constexpr uint8_t kIpsoCapacity    = 184; // 3384 - 3200
constexpr uint8_t kIpsoDcCurrent   = 185; // 3385 - 3200
constexpr uint8_t kIpsoDcVoltage   = 186; // 3386 - 3200

// Not a measurement — a 16-bit status bitfield the pack announces on sid 0x19 and 0x1A. It
// is decoded for its width only, so the walker stays aligned with any record behind it, and
// it deliberately contributes nothing to the all-zero template verdict: a non-zero status
// word must not license an untouched template, and a zero one must not veto a frame whose
// physical sensors did report.
// CITE(datasheet): [CIT-WISTOOLBOX-AT] at-specification-list-details.json @ byte 347583 —
//   `{"displayValue":"Bit Values (16bits)","sendValue":"243"}`. 243 is a bitfield, not a
//   quantity.
// CITE(bench): docs/EVIDENCE.md — the pack's announcement descriptor tail on afefec3 reads
//   `19 F3 08 00 1A F3 08 00`: two of this pack's six sensors are status words.
constexpr uint8_t kIpsoBitValues16 = 243;

// ---------------------------------------------------------------------------------------
// Frame location and verification.
// ---------------------------------------------------------------------------------------

// What a scan saw besides the frame it returned.
//
// The scan's boolean answer — "is there a verified frame here" — throws away the two facts
// that actually distinguish the failures. A frame whose declared length exceeds what arrived
// is a truncated read; a verified PROVISION frame where SENDAT was expected is the pack
// talking past us. Both used to collapse into BadFrame, and the collapse is what let a
// truncated 92-byte announcement be reported as a record set full of zeros.
struct ScanNotes {
    bool   bad_cksum     = false;
    bool   truncated     = false; // a candidate declared more bytes than arrived
    bool   saw_provision = false; // a verified PROVISION frame was present
    size_t declared      = 0;     // bytes the truncated candidate said it had, from the delimiter
    size_t arrived       = 0;     // bytes actually in the buffer
};

// A located, checksum-verified frame inside a receive buffer.
struct SnHubFrame {
    size_t  delim;       // index of the 0x7E
    size_t  payload;     // index of the first SensorHub byte
    size_t  payload_len; // RUI3 length field — covers the SensorHub frame and its records
    size_t  cksum;       // index of the trailing checksum byte
    uint8_t flag;        // RUI3 flag: request or response
    uint8_t dest;
    uint8_t source;
    uint8_t sequence; // SNHub_Api_t.sequence — echoed back on a genuine response
    uint8_t hub_type;
    uint8_t hub_payload_type;
};

// cal_chksum() over a frame already in a buffer: popcount of the RUI3 type byte, plus the
// RUI3 flag byte, plus every byte the length field covers. Shared by the verify path and the
// provisioning response so the two can never disagree about the algorithm.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.c cal_chksum().
uint8_t frame_chksum(const uint8_t *buf, size_t delim, size_t payload_len);

// Find the next verified frame at or after `from`.
//
// Scanning rather than assuming the buffer starts with one frame is not defensive padding —
// it is required. The bench capture shows a SENDAT reply and a provisioning announcement
// arriving back to back inside a single 64-byte read, so "the first delimiter in the buffer"
// is routinely the wrong frame.
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
bool next_frame(const uint8_t *buf, size_t len, size_t from, SnHubFrame &f, ScanNotes &notes);

// Is a complete, checksum-verified PROVISION request addressed to the master already in this
// buffer? Lets the drain return the instant the announcement is answerable instead of waiting
// out the inter-byte gap — see the rationale on Battery::receive().
bool provision_ready(const uint8_t *buf, size_t len);

// ---------------------------------------------------------------------------------------
// Response matching — issue #36.
// ---------------------------------------------------------------------------------------

// How strictly a SENDAT frame has to prove it is the one we are waiting for.
//
// The driver used to accept any structurally valid SENDAT frame as the answer to the query it
// had just issued. On a pack configured with a periodic rule that pushes unsolicited frames,
// that is not hypothetical: a spontaneous frame arriving inside the receive window would be
// taken as proof that the address just queried is live, and the driver would latch onto an
// address nothing is listening on.
enum class BatteryMatchMode : uint8_t {
    // Accept any SENDAT frame. Used only by the decoder tests, which exercise record walking
    // rather than addressing.
    Any,

    // Solicited: this frame must answer the request we just sent. Flag is RSP, destination is
    // the master, source is the address we queried, and the sequence is the one we sent.
    // CITE(prior-art): [CIT-RAK45WIRE] forest-weather-machines/rak-4-5-wire @ efc0e3c
    //   onewire_master_protocol.c — the master builds a response by swapping dest/source and
    //   flipping REQ to RSP while echoing the sequence untouched, so a genuine reply to our
    //   query is exactly dest=our source, source=our dest, flag=RSP, sequence=ours.
    Response,

    // Pack-initiated push. Flag is REQ and the destination is still the master, but the
    // sequence is the pack's own and cannot be predicted. The source is deliberately not
    // checked: an unprovisioned pack legitimately pushes from an id this master has not
    // learned yet, and rejecting that would discard the only frame the push listen exists to
    // catch.
    // CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.c
    //   protocol_list[SNHUB_TYPE_SENDAT].req = snhub_snsrdat_req_program — an unsolicited
    //   data push is a first-class part of this protocol.
    Unsolicited,
};

struct BatteryQueryMatch {
    BatteryMatchMode mode;
    uint8_t          addr;     // the address the query went to; a reply's source must equal it
    uint8_t          sequence; // the sequence that query carried

    // An explicit constructor rather than default member initialisers, because the two builds
    // do not agree about them. The device build compiles as gnu++11, where a class carrying
    // NSDMIs is not an aggregate and `BatteryQueryMatch{mode, addr, seq}` stops compiling
    // altogether; env:native compiles as gnu++17, where it is fine. The host tests therefore
    // cannot catch this class of break, which is worth stating out loud next to the fix: a
    // green `pio test -e native` is not evidence that the firmware compiles.
    BatteryQueryMatch(BatteryMatchMode m = BatteryMatchMode::Any, uint8_t a = 0, uint8_t s = 0)
        : mode(m), addr(a), sequence(s)
    {
    }
};

// Does this frame answer the outstanding request described by `match`?
bool frame_matches_query(const SnHubFrame &f, const BatteryQueryMatch &match);

// ---------------------------------------------------------------------------------------
// Record decoding.
// ---------------------------------------------------------------------------------------

// Everything the decoder observed that the caller may want to print. The codec cannot log —
// it has no Arduino — so the diagnostics that used to be LOGF calls inside the walker come
// back as fields and battery.cpp prints them. Nothing here changes the verdict; it only
// explains it.
struct BatteryFrameNotes {
    // Frame-scan level, copied from the ScanNotes of the scan that failed to find a SENDAT.
    bool   truncated_frame = false;
    size_t declared        = 0;
    size_t arrived         = 0;

    // A SENDAT frame was present but did not answer the outstanding request (issue #36).
    bool    unmatched          = false;
    uint8_t unmatched_flag     = 0;
    uint8_t unmatched_dest     = 0;
    uint8_t unmatched_source   = 0;
    uint8_t unmatched_sequence = 0;

    // Record-walk level.
    bool    truncated_record      = false;
    uint8_t truncated_record_type = 0;
    size_t  truncated_record_left = 0;
    bool    unknown_record        = false;
    uint8_t unknown_record_type   = 0;

    // The status bitfields, recorded so a capture shows what the pack puts in them. This pack
    // announces two; the cap is generous rather than exact.
    static constexpr uint8_t kMaxStatusWords = 4;
    uint8_t                  status_count    = 0;
    uint8_t                  status_sid[kMaxStatusWords]    = {0, 0, 0, 0};
    uint16_t                 status_value[kMaxStatusWords]  = {0, 0, 0, 0};
};

// Verify the buffer, find the SENDAT frame that answers `match`, and decode its records into
// `out`. Returns the reason when it refuses.
//
// `out` is cleared on entry and on every refusal, so a rejected frame can never leave values
// from an earlier read standing (issue #37). Validation matters more here than it looks: an
// unverified record scan will happily turn line noise into a plausible pack voltage, and a
// wrong battery reading is worse than none — it is the number a human uses to decide whether
// the node needs rescuing.
BatteryResult battery_decode_frame(const uint8_t *buf, size_t len, BatteryReading &out,
                                   BatteryFrameNotes &notes,
                                   const BatteryQueryMatch &match = BatteryQueryMatch{});

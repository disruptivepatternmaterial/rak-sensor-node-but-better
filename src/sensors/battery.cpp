#include "battery.h"

#include "../build_features.h"
#include "../power.h"

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

// Exactly one wake byte, because that is what the reference master emits and because on the
// provisioning path every extra byte is latency the pack is waiting through.
//
// `RUI3_Api_t.wakeup` is a single `U8` at the head of the struct, and the reference hands the
// whole struct to the transport in one call — so the frame on the wire is one 0xFF, one 0x7E,
// then the header. The previous four were a guess that the line needed settling; at 9600 they
// cost 4 x 1.04 ms = ~4.2 ms of dead air ahead of every frame, and ~3.1 ms of that is pure
// delay added between the pack finishing its announcement and hearing our answer. The pack
// tolerates four (the bench SENDAT sweep drew a full reply with them), so this is not the
// whole fault — but it is 3 ms off the critical path for free, and it removes a divergence
// from the one implementation known to be accepted by this pack.
//
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.h — RUI3_Api_t is
//   ATT_PACKED { U8 wakeup; U8 start; RUI3API_LEN_T length; type; flag; U8 payload[]; }: one
//   wakeup byte, not a run of them.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.c api_process()
//   rebuilds the frame as `dataBuff[0] = WAKEUPBYTE` followed by everything from the
//   delimiter on, and snhub_provision_req_program() hands that exact buffer to
//   SNHUBAPI_EVT_QSEND at `pktLen = len` — so the response leaves with a single 0xFF.
// CITE(prior-art): [CIT-MESHTASTIC-9154] @ 02050a4 RAK9154Sensor.cpp — the transport under
//   that event is `case SNHUBAPI_EVT_QSEND: mySerial.write(msg, len);`, one write of the
//   whole buffer with nothing prepended.
constexpr uint8_t kWakeCount = 1;

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
// The RUI3 length field is derived rather than constant: send_frame() computes it from the
// payload it is given. Every frame this driver now sends -- BOOT and SENDAT -- carries a
// zero-length payload and so transmits SHORT_SWAP(6) = 0x00 0x06.

// SensorHub frame fields.
constexpr uint8_t kHubTypeProvision = 0x01; // SNHUB_TYPE_PROVISION
constexpr uint8_t kHubTypeSendData  = 0x03; // SNHUB_TYPE_SENDAT
constexpr uint8_t kPldBoot          = 0x02; // PLD_PROVI_TYPE_BOOT
constexpr uint8_t kPayloadSendData  = 0x02; // PLD_SDATA_TPYE_SENDAT

// The per-sensor sampling rule, which is the thing this revision is here to reach.
//
// A provisioned probe still needs each of its sensors switched from RULE_DISABLE to
// RULE_PERIODIC before it samples anything; until then it answers a SENDAT with a
// well-formed record set full of placeholder zeros, which is precisely the production
// symptom. The reference exposes exactly one call for this — set.param(pid, sid, enable,
// intv) — and it is addressed per *sensor id*, not per probe: the sid goes in payload[0]
// and one request carries one sid.
//
// PLD_PARMGSET_TYPE_SNSR_UPDATE is the only payload type the reference's parameter command
// will emit; every other member of PLD_PARMGSET_TYPE_E falls through to `return RET_ERROR`,
// so RULE (0x03) — the one that reads like the obvious choice — is not actually reachable
// and must not be used.
//
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.h —
//   SNHUB_TYPE_PARAMSET = 2 and SNHUB_TYPE_PARAMGET = 5 in SNHUBAPI_TYPE_E;
//   PLD_PARMGSET_TYPE_E { PRB_INTV = 0x1, SNSR_INTV, RULE, SNSR_HTHR, SNSR_LTHR,
//   PRB_TAGID, PRB_TAGEN, PRB_UPDATE, SNSR_UPDATE = 0x9, CONF_UPDATE };
//   RULE_DISABLE 0x00 and RULE_PERIODIC 0x08.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.c
//   api_set_snsr_param(pid, sid, enb, intv): `menu.intv = intv; menu.rule = (enb == 0) ?
//   RULE_DISABLE : RULE_PERIODIC;` then snhub_paramget_command(pid, sid, SNHUB_GS_SET,
//   PLD_PARMGSET_TYPE_SNSR_UPDATE). That command sets hub_api->type = SNHUB_TYPE_PARAMSET,
//   payload_length = sizeof(SNHub_Api_Param_Snsr_t), payload[0] = sid, and only then fills
//   paramset->intv / paramset->rule — so the sid byte and the struct's `sid` field are the
//   same byte, and the addressing is per sensor.
constexpr uint8_t  kPldParamSnsrUpdate = 0x09; // PLD_PARMGSET_TYPE_SNSR_UPDATE
constexpr uint16_t kRuleDisable        = 0x0000;
constexpr uint16_t kRulePeriodic       = 0x0008;

// SNHub_Api_Param_Snsr_t, ATT_PACKED: sid(1) intv(4) rule(2) thr_above(10) thr_below(10)
// tag(16) = 43. The widths and order are the struct's, not a guess, and the total is what
// the reference puts in payload_length — so getting it wrong fails the pack's own
// verify_snhublen() and draws silence rather than a diagnosable error.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.h
//   SNHub_Api_Param_Snsr_t { U8 sid; U32 intv; U16 rule; U8 thr_above[10];
//   U8 thr_below[10]; U8 tag[16]; } ATT_PACKED; onewire_master_protocol.c
//   verify_snhublen() requires rui3_len == payload_length + sizeof(SNHub_Api_t).
constexpr size_t kParamSid   = 0;
constexpr size_t kParamIntv  = 1;
constexpr size_t kParamRule  = 5;
constexpr size_t kParamBytes = 43;

constexpr uint32_t kParamIntvMax     = 86400;

// How long to wait for the pack to acknowledge a parameter write, and how many times to
// repeat it.
//
// The previous revision fired PARAMSET once and drained for about 5 ms, on the reasoning
// that the reference library leaves `protocol_list[SNHUB_TYPE_PARAMSET]` with both `.req`
// and `.rsp` NULL and therefore expects no response at all. That reasoning is now known to
// be incomplete: RAK's own tooling treats a probe-configuration write as a request that must
// be *acknowledged*, budgeting 3000 ms for the acknowledgement and retrying up to 3 times.
// A 5 ms drain cannot observe a 3000 ms acknowledgement, so the firmware has never been in a
// position to know whether the pack answered — the write was not merely unacknowledged, it
// was unobservable.
//
// Matching RAK's budget is what makes the next capture diagnostic instead of ambiguous.
//
// CITE(datasheet): [CIT-WISTOOLBOX-AT] at-specification-list-details.json @ byte 389344 —
//   the `ATC+SNSR_CONF` command's config block is
//   `"config":{"retries":3,"success":"+EVT:UPD_CONF","timeout":3000}`. RAK allows three
//   attempts and three seconds per attempt for a probe-configuration write to be confirmed.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.c —
//   `protocol_list[SNHUB_TYPE_PARAMSET]` has `.req` and `.rsp` both NULL, so the reference
//   master discards any PARAMSET response rather than requiring one. Waiting for it is
//   therefore strictly additional evidence: a silent pack still falls through unharmed.
constexpr uint32_t kParamAckTimeoutUs = 3000000; // 3 s, RAK's own per-attempt budget
constexpr uint8_t  kParamAttempts     = 3;       // RAK's own retry count

// Ceiling on the whole enable pass, independent of the per-write budget above.
//
// Three attempts at three seconds across six announced sensors is 54 s of worst-case silence,
// and Battery::read() runs between two watchdog feeds in src/main.cpp — there is no feed
// inside it. Added to the 20 s push listen that is 74 s inside a 120 s window, before the
// weather read is counted. That is too close to a reset for a path whose whole purpose is
// diagnostics, and a watchdog reset would destroy the very capture this change exists to
// produce. The pass therefore checks a wall-clock deadline between sensors and stops early,
// logging that it did, rather than risking the reset.
//
// CITE(datasheet): [CIT-NRF-WDT] nRF52840 PS, WDT — the timeout is fixed at TASKS_START and
//   the watchdog cannot be stopped, so an awake path that overruns its window resets the
//   chip. src/main.cpp starts it at 120 s and feeds it around, not inside, the sensor reads.
constexpr uint32_t kEnablePassBudgetMs = 30000;

// The pack's self-announcement is a PROVISION request of payload type VER3 — and VER3 is
// the *only* provision payload type the reference master accepts. VER1, VER2 and BOOT all
// fall through to `return RET_ERROR`, so a driver that expects to be answered in one of
// those shapes will wait forever.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.h
//   PLD_PROVI_TYPE_E { VER1=0, VER2=1, BOOT=2, VER3=3 }; onewire_master_protocol.c:407-417
//   snhub_provision_req_program() switches on payload_type: `case PLD_PROVI_TYPE_VER3: /* do
//   nothing, just bypass */ break;` then VER1/VER2/BOOT/default `/* not support */ return
//   RET_ERROR`. The source's own word "bypass" means bypass the *rejection* — VER3 breaks out
//   of the switch and falls through into the record-and-echo body at :419-473, which is the
//   only path that ever reaches QSEND, ADD_PID or ADD_SID. Read as "VER3 is skipped" it says
//   the opposite of what it does, so state it the long way: VER3 is the one type answered.
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

// The rest of that struct, which is where the sampling rules live. Continuing the same
// walk: provId(1) at 22 + reserved1(7) = 30 for model_name, + model_name(20) = 50 for
// reserved2, + reserved2(4) = 54 for snsr_num, and the descriptor array begins at 55. Each
// descriptor is SNSRNODE { U8 sid; U8 ipso; U16 rule; } = 4 bytes.
//
// In the captured announcement the provision payload starts at frame index 12, so snsr_num
// lands at frame index 66 and the first descriptor at 67 — past the 64 bytes the previous
// scanner buffer retained, which is exactly why the rule values have never been seen. The
// declared payload leaves 79 bytes for a 55-byte struct, i.e. six descriptors, so the rule
// fields sit at frame indices 69, 73, 77, 81, 85 and 89.
//
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.h
//   SNHub_Api_Provision_t { hw_version, sw_version[3], SERIALNUM sn, provId, reserved1[7],
//   MDLNAME model_name, reserved2[4], snsr_num, SNSRNODE snsr_type[] } ATT_PACKED, with
//   SERIALNUM 18 bytes, MDLNAME 20 bytes and SNSRNODE { sid, ipso, U16 rule }.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.c
//   snhub_provision_req_program() reads `record[aid].snsrnum = hub_api_prov->snsr_num` then
//   `record[aid].snsrlist[i] = hub_api_prov->snsr_type[i].sid` — the announcement is the
//   only place the master ever learns which sensor ids a probe carries.
// CITE(bench): docs/EVIDENCE.md — announcement on 3d3425d declares hub payload_length 0x50
//   with model_name at frame index 42, fixing the provision payload base at index 12 and
//   therefore snsr_num at index 66. The tail was truncated by the scanner, not absent.
constexpr size_t kProvSnsrNumOffset = 54;
constexpr size_t kProvSnsrOffset    = 55;
constexpr size_t kSnsrNodeBytes     = 4;

// CITE(prior-art): [CIT-ONEWIRE-SERIAL] IPSO codes, already reduced by the 3200 offset.
//   These are the same numbers the payload encoder uses on the LoRaWAN side, because RAK
//   reuses the IPSO table in both places — which is a convenience, not a coincidence.
constexpr uint8_t kIpsoTemperature = 103; // 3303 - 3200
constexpr uint8_t kIpsoCapacity    = 184; // 3384 - 3200
constexpr uint8_t kIpsoDcCurrent   = 185; // 3385 - 3200
constexpr uint8_t kIpsoDcVoltage   = 186; // 3386 - 3200

// Not a measurement. Type 243 is a 16-bit status bitfield, and this pack announces two
// sensors carrying it (sid 0x19 and 0x1A). It matters twice over.
//
// First, the record walker has to know its width or it stops dead at the first one and
// discards every record behind it — and on this pack the two 243 sensors are announced last,
// so anything appended after them would be lost. "Bit Values (16bits)" is two bytes.
//
// Second, and more important, it must never count toward the all-zero template test below.
// A status word is a legitimate zero *and* a legitimate non-zero regardless of whether the
// pack has sampled anything: a non-zero one would let an untouched record template pass as a
// live measurement, and a zero one must not veto a frame whose physical sensors did report.
// So it is decoded for its width and deliberately contributes nothing to the verdict.
//
// CITE(datasheet): [CIT-WISTOOLBOX-AT] at-specification-list-details.json @ byte 347583 —
//   the sensor-type table lists `{"displayValue":"Bit Values (16bits)","sendValue":"243"}`
//   alongside `{"displayValue":"Bit Values (32bits)","sendValue":"244"}` and the physical
//   types `Temperature 103` / `DC Voltage 186`. 243 is a 16-bit bitfield, not a quantity.
// CITE(bench): docs/EVIDENCE.md — the pack's own announcement descriptor tail on afefec3
//   reads `19 F3 08 00 1A F3 08 00`: sid 0x19 and sid 0x1A both carry ipso 0xF3 = 243 with
//   rule 0x0008. Two of this pack's six sensors are status words.
constexpr uint8_t kIpsoBitValues16 = 243;

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

// Where bytes are read from, and where they are written to.
//
// One instance, one pin. An earlier revision could split the link across two pins
// (FEATURE_ONEWIRE_SPLIT) on the theory that bridging the pack's TXD and RXD onto one wire
// caused TX contention that corrupted the long provisioning frame. The bench settled it the
// other way: the pack talks perfectly well on the bridged harness -- it answers SENDAT with
// checksum-valid frames and identifies itself -- and what actually blocks provisioning is
// that the pack will not accept an id assigned over this link at all, from any topology.
// The split path was therefore removed rather than left switched off, because a second
// wiring mode that no evidence supports is a thing every future reader has to rule out.
//
// CITE(bench): docs/EVIDENCE.md 2026-08-04 -- on the bridged single-wire harness the pack
//   returns a 28-byte checksum-valid SENDAT reply and a 92-byte announcement, so contention
//   on the shared line is not preventing the pack from being heard.
// CITE(datasheet): [CIT-RAK2560] RAK2560 Hub Datasheet, "Pin Definition" -- a genuine master
//   drives pin 5 (one-wire UART) only and leaves pin 3 reserved, which is what a single
//   shared line reproduces.
SoftwareHalfSerial &link_for(uint8_t pin) { return bus(pin); }

constexpr uint32_t kFirstByteTimeoutUs = 500000; // probe wake can be slow
constexpr uint32_t kInterByteTimeoutUs = 5000;   // gap that ends a frame

// How long to wait for the pack to push a reading of its own accord.
//
// The poll only ever returns a record template, so the measurement has to come from the
// unsolicited report — and that arrives on the pack's own sampling cadence, not on ours. The
// previous 500 ms wait was therefore a bet that the two coincided, which the bench lost every
// cycle. This is deliberately generous because the cadence is configurable on the pack (it is
// exposed in RAK's WisToolBox) and is not readable from anything the firmware has been able to
// interrogate so far: PARAMGET is refused while the pack still considers itself unprovisioned.
//
// It costs awake time, which on a node meant to last months is not free — 20 s per cycle
// against an hourly interval is roughly 0.6% duty. Acceptable to prove the mechanism, and it
// should be narrowed to just over the pack's real cadence once that is known. Tracked with the
// rest of the battery bring-up in issue #5.
//
// Well inside the 120 s watchdog window in src/main.cpp, so a long listen cannot look like a
// hang.
// CITE(prior-art): [CIT-MESHTASTIC-9154] @ 02050a4 variants/rak2560/RAK9154Sensor.cpp —
//   onewireHandle() reschedules itself every 50 ms forever and drains on every tick, i.e. the
//   reference is *always* listening. A wake-transact-sleep driver has to buy an approximation
//   of that with a window wide enough to contain one push.
// CITE(bench): docs/EVIDENCE.md — stage3 on 246add8: the poll returned a checksum-valid
//   all-zero template every cycle and the 500 ms push listen that followed it caught nothing.
constexpr uint32_t kPushListenUs = 20000000; // 20 s

// One buffer, used by every phase in turn, and sized like the reference's.
//
// 96 was too small in two separate ways. The bench capture shows a 28-byte SENDAT reply and
// a 92-byte announcement arriving concatenated inside one read, which is 120 bytes — so a
// 96-byte buffer truncates the announcement that the frame scanner then has to skip, and the
// truncation itself looked like a checksum failure. And the announcement is the one frame
// whose *tail* matters now: the sampling rules live in its last 24 bytes, so anything that
// clips it clips the evidence.
//
// 0x100 is not a round number picked for comfort; it is BUFF_SIZE in the reference master,
// which is the largest frame the protocol is written to handle.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.c `#define
//   BUFF_SIZE 0x100`, used for both dataBuff[] and every per-command pktBuff[].
// CITE(bench): docs/EVIDENCE.md — SENDAT sweep on 3d3425d returned `FF 7E 00 15 ...`
//   (28 bytes) immediately followed by `FF 7E 00 55 ...` (92 bytes) in a single read.
constexpr size_t kRxCapacity = 0x100;

// How long to keep answering announcements before giving up on provisioning for this cycle.
//
// This replaces a three-attempt loop that answered the first announcement and immediately
// moved on to polling. Every other explanation for the pack never latching the id has now
// been eliminated by the bench: the response frame is byte-for-byte what the reference emits
// (checksum arithmetic independently confirmed), VER3 is the type the reference answers, BOOT
// is the only frame a master originates, and parameter writes are not involved. What is left
// is the one behavioural difference between our driver and a real master — a real master
// never stops answering.
//
// The reference is explicit about this. Re-entering snhub_provision_req_program() with a
// serial number it has already recorded takes the `f_memcmp(...) == 0` break, skips the
// re-copy, and still echoes provId and still re-fires ADD_PID. Answering the same probe
// repeatedly is not a degenerate case in that code, it is the steady state. Meshtastic then
// reschedules the handler every 50 ms for as long as the board is powered, so the answer
// arrives for every announcement, indefinitely. Our wake-transact-sleep driver cannot do
// "indefinitely", so it buys a window instead.
//
// 45 s because the pack re-announces on its own cadence and this has to span several of those
// periods for "answered repeatedly" to be a real test rather than a re-run of "answered once".
//
// THE POWER COST IS NOT AFFORDABLE AS A STEADY STATE. 45 s of radio-silent awake time every
// wake, on a node meant to run for months on a solar-recharged pack, is a bring-up measure and
// nothing more — see docs/POWER_BUDGET.md. The intended end state is to provision once and
// persist the latched id, or at minimum to run the long window only while the pack is not yet
// latched and a short one afterwards; both are deliberately not done here, because the point
// of this revision is to find out whether the latch happens at all, and a shortcut taken
// before that is known would be a shortcut around the evidence. Tracked with the rest of the
// battery bring-up in issue #5.
//
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.c
//   snhub_provision_req_program(): the already-known-serial-number path breaks out of the
//   record search rather than returning, so it falls through to the same body that sets
//   `hub_api_prov->provId = pid`, recomputes the checksum, raises SNHUBAPI_EVT_QSEND and then
//   SNHUBAPI_EVT_ADD_PID. A repeat announcement from a known probe is answered exactly like
//   the first one.
// CITE(prior-art): [CIT-MESHTASTIC-9154] @ 02050a4 variants/rak2560/RAK9154Sensor.cpp —
//   onewireHandle() ends `return 50;`, so the scheduler re-runs it every 50 ms forever, and
//   the drain plus process() runs unconditionally on every tick. The one consumer this pack is
//   known to accept never stops listening and never stops answering.
// CITE(bench): docs/EVIDENCE.md — the announcement carries provId 0xFF on every cycle across
//   every previous revision, i.e. the pack has never been observed to latch the id it was
//   handed. That is the observation this window exists to change or to rule out.
// CITE(datasheet): [CIT-NRF-WDT] nRF52840 PS, WDT — the watchdog cannot be stopped once
//   started, so this window is sized against the 120 s timeout src/main.cpp arms rather than
//   against patience. Worst case for the whole of Battery::read() is roughly 66 s: a ~0.5 s
//   direct probe, this 45 s window, two ~0.5 s poll attempts, and the 20 s push listen. That
//   no longer has to fit between two feeds -- acquire_pid() and receive() now feed the
//   watchdog themselves -- but it is still the awake time the power budget pays for, and it
//   is why the direct probe runs first: a provisioned pack never enters this window at all.
constexpr uint32_t kProvWindowMs = 45000;

// SensorHub frame header (inside the RUI3 payload) before the first record: dest, source,
// sequence, hub-type, payload-length, payload-type.
constexpr size_t kHubHeaderBytes = 6;

// The provisioning announcement is far larger than a data reply: the captured one declares
// a payload length of 85, so the frame runs 92 bytes, and it grows with the probe's sensor
// count. It also has to be answered by echoing it back, so it must be buffered whole — a
// truncated announcement cannot be turned around.
// CITE(bench): docs/EVIDENCE.md — announcement on 3d3425d declares length 0x0055 = 85,
//   giving delimiter + 5 header + 85 + 1 checksum = 92 bytes from the wake byte.
constexpr size_t kProvCapacity = kRxCapacity;

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

// Is the frame we are here to answer already complete in this buffer?
//
// This is the difference between answering the announcement and answering it too late. Our
// drain loop cannot tell "the pack has stopped talking" from "the pack is between bytes", so
// it waits out the whole inter-byte gap before returning — and only then does the caller get
// to reply. The reference never pays that: its drain is `while (available()) { read();
// delay(2); }`, which at 9600 polls slower than bytes arrive, so `available()` stays true for
// the length of the frame and goes false roughly one byte-time after the last one. It then
// replies immediately.
//
// The declared length lets us do better than a timeout: the moment the buffer holds a
// checksum-verified PROVISION request addressed to the master, there is nothing left to wait
// for. Deliberately narrow — a data reply must NOT short-circuit the drain, because the pack
// routinely concatenates its spontaneous announcement behind a SENDAT response and stopping at
// the first complete frame there would discard the second one.
//
// CITE(prior-art): [CIT-MESHTASTIC-9154] @ 02050a4 RAK9154Sensor.cpp onewireHandle() —
//   `while (mySerial.available()) { buff[bufflen++] = mySerial.read(); delay(2); }` then
//   `RakSNHub_Protocl_API.process(buff, bufflen)` on the same tick. The reply is transmitted
//   within about one byte-time of the announcement's last byte, not after a frame-gap timeout.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.c api_process()
//   validates in the order verify_checksum -> verify_rui3type -> verify_snhublen and only then
//   dispatches; a frame is answerable as soon as those hold, which is as soon as the byte at
//   payload[length] has arrived. Same condition, evaluated as the bytes land.
bool provision_ready(const uint8_t *buf, size_t len)
{
    SnHubFrame f;
    bool       bad_cksum = false;
    size_t     from      = 0;

    while (next_frame(buf, len, from, f, bad_cksum)) {
        from = f.delim + 1;
        if (f.hub_type == kHubTypeProvision && f.flag == kRui3FlagReq &&
            f.hub_payload_type == kPldProvVer3 && f.dest == kMasterId) {
            return true;
        }
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
    link_for(m_pin).write(b);
}

// Compose and transmit one request. `payload` is the SensorHub payload that follows the
// six-byte header — empty for BOOT and SENDAT, one sid byte for PARAMGET, a 43-byte
// SNHub_Api_Param_Snsr_t for PARAMSET.
//
// The RUI3 length field covers the SensorHub header *and* that payload, and the pack checks
// the two against each other: verify_snhublen() requires the RUI3 length to equal
// payload_length + 6, so the header's payload_length byte and the transport length cannot be
// filled in independently. Both are derived from one number here for that reason.
//
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.c
//   snhub_paramget_command(): `pktLen = sizeof(SNHub_Api_t); pldLen += ...;
//   pktLen += pldLen; hub_api->payload_length = pldLen;` then
//   `rui3_api->length.value = SHORT_SWAP(pktLen)` — one length, two places.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.c
//   verify_snhublen() accepts only rui3_len == payload_length + sizeof(SNHub_Api_t) (or that
//   minus one, for older firmware that counted the checksum byte).
void Battery::send_frame(uint8_t dest, uint8_t hub_type, uint8_t payload_type,
                         const uint8_t *payload, size_t payload_len)
{
    // SensorHub frame: dest, source, sequence, hub-type, payload-length, payload-type.
    const uint8_t seq    = ++m_seq;
    const uint8_t hub[6] = {dest,     kMasterId,            seq,
                            hub_type, (uint8_t)payload_len, payload_type};

    // SHORT_SWAP(pktLen) puts the high byte on the wire first, which is why the verified
    // zero-payload case transmits 0x00 then 0x06 for a total of six.
    const size_t  total  = sizeof(hub) + payload_len;
    const uint8_t len_hi = (uint8_t)(total >> 8);
    const uint8_t len_lo = (uint8_t)(total & 0xFF);

    // Checksum is NOT an XOR. It is the sum of the set-bit counts (popcount) of the RUI3
    // type byte, the RUI3 flag byte, and every byte the length field covers (the SensorHub
    // header plus the payload), accumulated into a uint8_t.
    // CITE(prior-art): [CIT-ONEWIRE-SERIAL] onewire_master_protocol.c cal_chksum():
    //   chsum = popcount(type) + popcount(flag) + sum popcount(payload[0..len-1]).
    uint8_t checksum = (uint8_t)(__builtin_popcount(kRui3TypeSensorHub) +
                                 __builtin_popcount(kRui3FlagReq));
    for (size_t i = 0; i < sizeof(hub); i++) {
        checksum += __builtin_popcount(hub[i]);
    }
    for (size_t i = 0; i < payload_len; i++) {
        checksum += __builtin_popcount(payload[i]);
    }

    for (uint8_t i = 0; i < kWakeCount; i++) {
        tx_byte(kWakeByte);
    }
    tx_byte(kDelimiter);
    tx_byte(len_hi);
    tx_byte(len_lo);
    tx_byte(kRui3TypeSensorHub);
    tx_byte(kRui3FlagReq);
    for (size_t i = 0; i < sizeof(hub); i++) {
        tx_byte(hub[i]);
    }
    for (size_t i = 0; i < payload_len; i++) {
        tx_byte(payload[i]);
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
    link_for(m_pin).flush(); // anything still queued predates this request
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
bool Battery::provision(uint8_t *buf, size_t len, uint8_t &announced_provid)
{
    SnHubFrame f;
    bool       bad_cksum = false;
    size_t     from      = 0;

    announced_provid = kBroadcastId; // "still unprovisioned" until a frame says otherwise

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

        // Read the id the pack believes it has, before the mutation below overwrites it.
        //
        // This one byte is the whole verdict on the handshake and it has been free all along.
        // The master assigns an id by echoing it back; the pack proves it accepted the
        // assignment by carrying that id in its *next* announcement instead of 0xFF. Every
        // capture so far shows 0xFF, which is how we know nothing has ever stuck — but the
        // driver read it, overwrote it, and never reported it, so the fact had to be recovered
        // by hand from hex dumps every single time. Surfaced to the caller so the provisioning
        // loop can stop the instant it changes.
        //
        // CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.h
        //   SNHub_Api_Provision_t.provId, and onewire_master_protocol.c
        //   snhub_provision_req_program() which writes `hub_api_prov->provId = pid` into the
        //   frame it echoes. The field is the master's assignment on the way out and the
        //   probe's own belief about its id on the way in.
        // CITE(bench): docs/EVIDENCE.md — every announcement captured to date carries 0xFF at
        //   this offset, on every revision, including the cycles where the response was
        //   transmitted correctly. Unlatched is the observed state, not an assumed one.
        announced_provid = buf[prov + kProvIdOffset];

        // Mutate and transmit FIRST. Nothing — not one log line — goes between recognising the
        // announcement and putting the answer on the wire.
        //
        // This ordering is the fix. The previous revision decoded and printed the six sensor
        // descriptors here, seven LOGF lines, and only then transmitted. LOG goes to USB CDC,
        // and a CDC write blocks until the host drains the endpoint FIFO — milliseconds per
        // line when a host is attached, and unbounded when one is attached but not reading. So
        // the reply that this driver correctly composed was leaving tens of milliseconds after
        // the announcement it answers, by which time the pack had given up on being provisioned
        // and moved on to re-announcing. Byte for byte the frame was right; it was simply late.
        //
        // The reference has no such gap and, tellingly, its author commented out the two
        // LOG_INFO calls on exactly this path. The descriptors are still read and still logged
        // — just afterwards, out of the timing-critical window. They survive the mutation
        // because the five bytes it changes (flag, dest, source, provId, checksum) all sit
        // outside the descriptor array.
        //
        // CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.c
        //   snhub_provision_req_program(): the recomputed buffer goes out via
        //   `on_evt(source, 0, SNHUBAPI_EVT_QSEND, pktBuff, pktLen)` and only then does it
        //   raise SNHUBAPI_EVT_ADD_PID and the per-sensor SNHUBAPI_EVT_ADD_SID events. Send
        //   first, account for it second — the order is the reference's, not a preference.
        // CITE(prior-art): [CIT-MESHTASTIC-9154] @ 02050a4 RAK9154Sensor.cpp — QSEND is
        //   `mySerial.write(msg, len)` with nothing before it, while the ADD_PID and ADD_SID
        //   cases carry their `LOG_INFO(...)` commented out. The one consumer known to be
        //   accepted by this pack prints nothing on this path.
        // CITE(datasheet): [CIT-NRF-USBD] nRF52840 Product Specification, USBD — bulk IN
        //   transfers only drain when the host issues an IN token, so a device-side write to a
        //   CDC endpoint whose FIFO is full stalls until the host polls. Serial logging is not
        //   a constant-time operation and must not sit inside a protocol response window.
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

        // Reworded from "provisioned probe ... as pid ...", which overstated what had
        // happened: it announced an assignment the pack had not been observed to accept. The
        // announced id is now printed beside it, so every line says which of the two facts it
        // is — we answered, versus the pack agreed.
        LOGF("   battery : answered probe 0x%02X (announced pid 0x%02X) with pid 0x%02X\n",
             f.source, announced_provid, kProbeId);

        // The exact bytes we just transmitted, so the next capture can be compared against the
        // announcement that provoked them rather than trusted. Printed from the wake byte
        // onward in the order it left, including the recomputed checksum.
        LOGF("   battery : reply %02X", kWakeByte);
        for (size_t i = f.delim; i <= f.cksum; i++) {
            LOGF(" %02X", buf[i]);
        }
        LOGLN("");

        // Now the descriptors, out of the response window. This is still the only place the
        // pack ever states which sensors it carries and what rule each one runs under, and it
        // is what tells the next capture whether the zeros were ever a sampling problem: rule
        // 0x00 (RULE_DISABLE) means provisioned-but-idle, rule 0x08 (RULE_PERIODIC) means the
        // sensors are armed and the zeros came from somewhere else.
        if (prov + kProvSnsrNumOffset < f.cksum) {
            const uint8_t announced = buf[prov + kProvSnsrNumOffset];
            LOGF("   battery : probe 0x%02X announces %u sensor(s)\n", f.source, announced);
            for (uint8_t s = 0; s < announced; s++) {
                const size_t at = prov + kProvSnsrOffset + (size_t)s * kSnsrNodeBytes;
                if (at + kSnsrNodeBytes > f.cksum) {
                    LOGF("   battery : descriptor %u truncated at %u bytes — tail lost\n", s,
                         (unsigned)len);
                    break;
                }
                // SNSRNODE.rule is a U16 in a packed struct on a little-endian core, so the
                // low byte leads. Both bytes are printed as well as the decoded value so a
                // wrong assumption here is visible in the capture rather than silent.
                const uint16_t rule = (uint16_t)(buf[at + 2] | ((uint16_t)buf[at + 3] << 8));
                LOGF("   battery :   sid 0x%02X ipso %u rule 0x%04X (%02X %02X) %s\n",
                     buf[at + 0], buf[at + 1], rule, buf[at + 2], buf[at + 3],
                     rule == kRulePeriodic  ? "periodic"
                     : rule == kRuleDisable ? "DISABLED"
                                            : "?");
            }
        }
        return true;
    }
    return false;
}

// Hex-dump a buffer under a label. Every failure path in this driver ends in "the next bench
// run has to be able to answer this", and a decoded verdict without the bytes behind it
// cannot be re-examined once the console has scrolled.
void Battery::dump(const char *what, const uint8_t *buf, size_t len)
{
    LOGF("   battery : %s", what);
    for (size_t i = 0; i < len; i++) {
        LOGF(" %02X", buf[i]);
    }
    LOGLN("");
}

// Stay in the provisioning phase, answering every announcement, the way a real master does.
//
// The previous revision answered the first announcement and then moved on to polling within
// milliseconds. That is the last structural difference between this driver and a master the
// pack is known to accept, and it is now the only surviving explanation for the pack never
// latching the id: everything else has been eliminated. The response bytes match the
// reference exactly, VER3 is the type the reference acts on, BOOT is the only frame a master
// originates, and the parameter writes were ours alone. What we have never done is keep
// answering.
//
// The reference's steady state is a loop, not a transaction. Meshtastic reschedules the
// one-wire handler every 50 ms for as long as the board is powered and drains on every tick,
// and the protocol code answers a repeat announcement from an already-known probe exactly as
// it answers the first — the already-recorded path breaks out of the serial-number search and
// falls through into the same echo-and-ADD_PID body. So the pack, in the configuration where
// it is known to work, is answered over and over. If it only latches after several answered
// announcements, or requires the master to still be answering when it next announces, a driver
// that answers once and leaves could never reach that state no matter how correct its bytes
// were.
//
// Structure:
//   * BOOT once, at the top, and never again inside the window. Its documented effect is to
//     make probes re-announce; the pack already re-announces on its own, and repeating a
//     command whose purpose is to restart announcement is a plausible way to reset whatever
//     state is accumulating across answered announcements. Nothing in the reference repeats
//     it either — api_init() sends it at startup and api_set_provision() on an explicit
//     request, never on a timer.
//   * Then drain-and-answer until the deadline, with the same early-exit drain and the same
//     mutate-and-transmit response as before. Nothing about the frame changes.
//   * Break the moment the pack proves it latched (see below), because at that point the
//     window has served its purpose and every further second is pure battery cost.
//
// No flush() inside the loop, deliberately: the receive path may be part-way through an
// announcement when an iteration ends, and discarding those bytes would manufacture exactly
// the missed-announcement failure this phase exists to remove.
//
// CITE(prior-art): [CIT-MESHTASTIC-9154] @ 02050a4 variants/rak2560/RAK9154Sensor.cpp —
//   onewireHandle() ends `return 50;` so the scheduler re-runs it every 50 ms indefinitely,
//   with `while (mySerial.available()) { ... }` and `RakSNHub_Protocl_API.process()` on every
//   tick and no exit condition. Continuous answering is the reference's normal operation.
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.c — BOOT is sent
//   only from api_init() and api_set_provision(), both as
//   snhub_provision_command(PID_UNKNOW, 0, SNHUB_GS_SET, PLD_PROVI_TYPE_BOOT), and nothing
//   awaits a reply. Announcements arrive independently afterwards, which is why one BOOT is
//   enough and why repeating it buys nothing.
// CITE(bench): docs/EVIDENCE.md — a 20 s passive listen at 9600 that transmitted nothing at
//   all still received the announcement, repeatedly and unchanged with provId 0xFF. The pack
//   does not need prompting; it needs answering.
bool Battery::acquire_pid(uint8_t *buf, size_t cap)
{
    link_for(m_pin).flush(); // anything queued predates this window
    send_boot();
    delay(2); // let the probe turn the line around

    const uint32_t started_ms = millis();
    bool           answered   = false;
    uint8_t        answers    = 0;

    while ((millis() - started_ms) < kProvWindowMs) {
        // This loop can hold the CPU for the full 45 s window, which is over a third of the
        // 120 s watchdog. Add the join backoff and a slow RK900 read on the same wake and the
        // total crosses it — so the watchdog would reset a node that is working exactly as
        // designed, and the reset would look like a hang rather than a budget overrun. Feeding
        // here is not weakening the watchdog: this loop makes forward progress on a bounded
        // deadline, and the deadline is what stops it, not the reset.
        // CITE(spec): docs/FIRMWARE_SPEC.md §7 H1 — 120 s hardware watchdog.
        power::watchdog_feed();

        // Early-exit drain: answer the announcement as soon as it is complete, not after the
        // frame gap. This is the second half of the latency fix — see provision_ready().
        // A quiet slice returns 0 after the first-byte timeout, which is what paces this loop.
        const size_t n = receive(buf, cap, /*stop_on_provision=*/true);
        if (n == 0) {
            continue;
        }

        uint8_t announced = kBroadcastId;
        if (!provision(buf, n, announced)) {
            // Bytes arrived but held no answerable announcement. Worth the dump: it separates
            // "the pack is quiet" from "the pack said something this parser rejected".
            dump("prov?", buf, n);
            continue;
        }

        answered = true;
        answers++;

        // The latch test, and the reason this loop can be short-lived when it works.
        //
        // An announcement carrying anything other than PID_UNKNOW proves the pack accepted an
        // assignment: that field is its own belief about its id, and it has read 0xFF in every
        // capture ever taken from this pack. PID_MASTER is excluded because 0x00 is the
        // master's address and cannot be a probe's id, so a 0x00 there is a decoding fault
        // rather than a latch. The observed value is logged either way, so a latch onto some
        // third id shows up as itself instead of being flattened into "worked".
        //
        // CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.h
        //   PID_UNKNOW = 0xFF is the id an unprovisioned probe carries and PID_MASTER = 0x00
        //   is the master's own address; onewire_master_protocol.c api_set_snsr_param()
        //   refuses `pid == PID_MASTER` outright, so neither value can be a probe's assigned
        //   id.
        // CITE(bench): docs/EVIDENCE.md — the announcement's provId has read 0xFF on every
        //   cycle of every revision, including ones that transmitted a byte-correct response,
        //   so any other value in this field is new information rather than noise.
        const bool latched = (announced != kBroadcastId && announced != kMasterId);
        if (latched) {
            m_pack_latched = true;
            m_pid          = announced;
            // The line that settles the question. One grep for "pack latched" tells the next
            // bench run whether sustained answering is what the pack was waiting for.
            LOGF("   battery : pack latched pid 0x%02X after %u answer(s), %lu ms\n", announced,
                 (unsigned)answers, (unsigned long)(millis() - started_ms));
            break;
        }

        m_pid = kProbeId;
    }

    if (!answered) {
        return false;
    }

    if (!m_pack_latched) {
        // Answered repeatedly and the pack still says 0xFF. That is a negative result worth
        // stating plainly rather than leaving to be inferred from an absent line: it means
        // sustained answering is not the missing piece either, and the next hypothesis has to
        // come from somewhere other than the master's timing.
        LOGF("   battery : answered %u announcement(s) in %lu ms — pack still reports pid "
             "0x%02X\n",
             (unsigned)answers, (unsigned long)(millis() - started_ms), kBroadcastId);
    }

    delay(50); // the reference's tick interval — the only timing guidance available
    return true;
}

// Drain whatever the GPIOTE receiver has buffered. Bytes are assembled in the interrupt
// handler and queued, so this no longer has to be sitting on the pin when the start bit
// arrives — the previous polling loop could miss a reply simply by being one bit late.
//
// CITE(prior-art): [CIT-MESHTASTIC-9154] @ 02050a4 RAK9154Sensor.cpp onewireHandle():
//   `while (mySerial.available()) { buff[bufflen++] = mySerial.read(); delay(2); }` — a
//   per-byte grace period is what delimits the frame, because there is no length field we
//   can trust before parsing. Same shape here, with the gap expressed as a timeout.
size_t Battery::receive(uint8_t *buf, size_t cap, bool stop_on_provision,
                        uint32_t first_byte_timeout_us)
{
    SoftwareHalfSerial &link = link_for(m_pin);

    // The first byte gets the long window: the probe may still be waking. Callers waiting on
    // an unsolicited push override it, because that wait is bounded by the pack's sampling
    // cadence rather than by its wake time.
    uint32_t deadline_us =
        (first_byte_timeout_us > 0) ? first_byte_timeout_us : kFirstByteTimeoutUs;
    uint32_t mark        = micros();
    size_t   n           = 0;

    // Smallest buffer that could possibly hold an answerable frame: delimiter + length[2] +
    // type + flag + the six-byte SensorHub header + a checksum byte. Below that the scan
    // cannot succeed, so running it on every byte is wasted work in the one loop that must
    // keep up with the wire.
    constexpr size_t kMinFrame = 1 + 4 + kHubHeaderBytes + 1;

    while (n < cap) {
        // The push-listen caller hands this loop kPushListenUs — 20 s — and it spins here
        // without touching anything else. That is a sixth of the watchdog window spent inside
        // one function, and it stacks with the provisioning window and a slow RK900 read on
        // the same wake. Feeding is safe because the loop cannot run past its own deadline:
        // the timeout below is what ends it, so a genuinely stuck line still returns.
        // CITE(spec): docs/FIRMWARE_SPEC.md §7 H1 — 120 s hardware watchdog.
        power::watchdog_feed();

        const int v = link.read();
        if (v >= 0) {
            buf[n++] = (uint8_t)v;
            // Return the instant the announcement is complete rather than waiting out the
            // inter-byte gap. The gap is a guess about whether the pack has finished; the
            // declared length is a fact, and on this path the 5 ms difference is 5 ms the pack
            // spends waiting to be provisioned. See provision_ready().
            if (stop_on_provision && n >= kMinFrame && provision_ready(buf, n)) {
                return n;
            }
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
    // Whether any *physical* record in this frame carried a non-zero value. This is the
    // template detector, and it has to be accumulated during the walk rather than
    // reconstructed from `out` afterwards, because `out` cannot distinguish "the pack
    // reported 0" from "no record of that type was present".
    //
    // "Physical" is load-bearing. Only voltage, current, capacity and temperature count
    // toward it. The 16-bit status bitfields this pack also carries are excluded in both
    // directions: a non-zero status word must not license an otherwise untouched record
    // template to be reported as a live measurement, and a zero status word must not veto a
    // frame whose voltage and current did report. A bitfield says nothing about whether the
    // pack has sampled.
    bool any_nonzero = false;

    size_t i = records;
    while (i + 2 < records_end) {
        const uint8_t type = buf[i + 1];

        if (type == kIpsoCapacity) {
            out.soc.set(buf[i + 2]);
            any_nonzero |= (buf[i + 2] != 0);
            i += 3;
        } else if (type == kIpsoDcCurrent && (i + 3) < records_end) {
            out.current.set((int16_t)val16(i + 2));
            any_nonzero |= (val16(i + 2) != 0);
            i += 4;
        } else if (type == kIpsoDcVoltage && (i + 3) < records_end) {
            out.voltage.set(val16(i + 2));
            any_nonzero |= (val16(i + 2) != 0);
            i += 4;
        } else if (type == kIpsoTemperature && (i + 3) < records_end) {
            // Type 103 is the same code the pack's own LoRaWAN uplinks use for temperature,
            // a signed 16-bit value. Byte order follows the flag like the others; the
            // Meshtastic reader does not decode this type, so the sign is still unconfirmed
            // against a non-zero reading.
            out.temperature.set((int16_t)val16(i + 2));
            any_nonzero |= (val16(i + 2) != 0);
            i += 4;
        } else if (type == kIpsoBitValues16 && (i + 3) < records_end) {
            // Stepped over, not stored and not counted. Decoding its width is what keeps the
            // walker aligned so any record behind it is still readable; everything else about
            // it is deliberately ignored. Logged so the next capture shows what the pack puts
            // in these two words, which is currently unknown — it is the only field on this
            // pack whose meaning is still open. See kIpsoBitValues16.
            LOGF("   battery : status word sid 0x%02X = 0x%04X (not a measurement)\n",
                 buf[i], val16(i + 2));
            i += 4;
        } else if (type == kIpsoDcCurrent || type == kIpsoDcVoltage ||
                   type == kIpsoTemperature || type == kIpsoBitValues16) {
            // Recognized type, but the frame ends before its payload does — the four branches
            // above all require two value bytes and this one has fewer left.
            //
            // Reported separately because it is a different fault with a different fix. The
            // branch below means "the pack speaks a record this build has never heard of",
            // which is answered by adding a decoder. This means "the frame was cut short",
            // which is answered by looking at the transport: a truncating receive buffer, an
            // inter-byte gap that expired mid-record, or a length field that disagrees with
            // what arrived. Collapsing the two would have sent the next reader to write a
            // parser for type 185, which is already parsed here.
            LOGF("   battery : record type %u truncated — %u byte(s) left, needs 4\n", type,
                 (unsigned)(records_end - i));
            break;
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
    // The sentinel is "every record in this frame read zero", not "the voltage read zero".
    // Voltage alone was the previous test and it leaves a hole: a SENDAT reply that carries
    // capacity, current and temperature but no voltage record has no sentinel to trip, so a
    // template full of placeholders would be encoded as a real 0 A / 0 % / 0.0 degC reading.
    // That is precisely the fabricated zero the repo's null policy exists to forbid, and it
    // is reachable — the record set is whatever the pack chooses to include, not a fixed
    // four.
    //
    // Judging the whole frame instead keeps every genuine zero: 0 A is a real idle current,
    // 0 % is a real (alarming) charge state, and 0.0 degC is an ordinary temperature in the
    // woods — each of those survives as long as one other record in the same frame is
    // non-zero, which for a pack that is powered by the cell it measures is always true of
    // the voltage. Only the all-zero case, which cannot be a live measurement from a device
    // that is simultaneously driving this wire, is discarded.
    //
    // Discarded wholesale rather than field by field: if one value is a placeholder the
    // others beside it are placeholders too, and a partially trusted record set is how a
    // plausible-looking wrong number reaches an uplink.
    //
    // CITE(bench): docs/EVIDENCE.md — the SENDAT reply captured on 3d3425d verified its
    //   checksum and decoded cleanly to voltage 0, current 0, capacity 0, temperature 0 while
    //   the pack was demonstrably alive, metered at 11.6 V, and re-announcing itself
    //   unprovisioned. A live pack cannot be at 0.00 V and also be driving this line.
    // CITE(prior-art): [CIT-MESHTASTIC-9154] @ 02050a4 RAK9154Sensor.cpp — the reference
    //   consumer applies no such guard: it assigns dc_vol/dc_cur/dc_prec straight from the
    //   frame. It gets away with it because it re-reads forever on a 50 ms tick and a stale
    //   zero is overwritten seconds later. A node that wakes, reads once and sleeps for an
    //   hour has no such second chance, so the guard is added here deliberately.
    if (!any_nonzero) {
        out = BatteryReading{};
        return BatteryResult::Unsampled;
    }

    return BatteryResult::Ok;
}

BatteryReading Battery::read()
{
    BatteryReading out;

    // The pack's 3V3 reference is wired to the always-on VDD pad, not to the switched 3V3_S
    // rail WB_IO2 gates, so there is deliberately no rail to raise here. That is a wiring
    // decision: rk900.cpp drops WB_IO2 after the weather read, and routing the pack through
    // the same rail would kill its reference mid-cycle.
    // CITE(datasheet): [CIT-RAK19007] RAK19007 Datasheet — "IO2 controls the power switch of
    //   3V3_S", which is a different net from the VDD pad the pack's pin 4 sits on.

    // begin() caches the port registers, arms the GPIOTE falling-edge interrupt, and leaves
    // the pin as input-with-pull-up so the idle line reads high. Everything after this point
    // is timing-critical only inside the library.
    //
    // CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 src/SoftwareHalfSerial.cpp — begin()
    //   ends with `listen()`, and listen() starts `if (active_object) active_object->
    //   stopListening();`. Only the most recent begin() keeps the interrupt, which is why
    //   one instance on one pin is the whole story here.
    SoftwareHalfSerial &link = link_for(m_pin);
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
    // One buffer for every phase, reused in turn. Two separate 256-byte arrays would put half
    // a kilobyte on the stack for no benefit, and the phases never overlap.
    static uint8_t rx[kRxCapacity];
    static_assert(kProvCapacity <= kRxCapacity, "provisioning needs the whole buffer");

    // Phase 0: ask the provisioned pack directly, before spending anything on provisioning.
    //
    // Once the pack has been provisioned through WisToolBox (docs/DEPLOY.md) it holds a real
    // probe id and stops announcing itself as unprovisioned. Phase 1 below is built entirely
    // around hearing that announcement — so on a provisioned pack it hears nothing, and burns
    // the full kProvWindowMs (45 s) doing it, on every single wake, forever. That is the
    // difference between a sub-second cycle and a 45-second one, and at an hourly interval it
    // is the difference between a power budget that works and one that does not.
    //
    // One SENDAT to kProbeId costs at most kFirstByteTimeoutUs (500 ms) when the pack is not
    // there, which is the price an unprovisioned pack pays once per cycle to keep the
    // provisioned case fast. That is the right way round: the provisioned pack is the field
    // configuration, and the unprovisioned one is a bench state on its way out of existence.
    //
    // No persisted flag backs this. It is a probe, not a memory, so it is self-healing by
    // construction: a replacement pack that has never been provisioned simply fails the probe
    // and falls through to phase 1, and a pack provisioned later starts answering with no
    // reset and nothing to clear. A stored "this pack is provisioned" bit would have to be
    // invalidated by hand the first time the hardware changed, and the failure mode of a stale
    // one is silence that looks like a dead sensor.
    //
    // Ok and Unsampled are both accepted as proof of address, exactly as phase 2 does: one
    // carries a measurement, the other the record template, and both are SENDAT frames that
    // came back from the destination we addressed. Only the address is being established here.
    //
    // CITE(bench): docs/EVIDENCE.md 2026-08-04 — sixteen consecutive byte-correct provisioning
    //   responses left provId at 0xFF, establishing that provisioning does not complete over
    //   this link and that the announcement phase has nothing to accomplish on a pack RAK's own
    //   tooling has already configured.
    // CITE(datasheet): [CIT-WISTOOLBOX-AT] — probe configuration is a north-bound ATC+
    //   operation over NFC/BLE, so a provisioned pack's id arrives from outside this firmware
    //   and can only be discovered by asking.
    // CITE(prior-art): [CIT-MESHTASTIC-9154] @ 02050a4 variants/rak2560/RAK9154Sensor.cpp —
    //   after ADD_PID the reference polls SENDAT against the known id and never re-runs
    //   provisioning. Steady state is a bare query.
    // The reply is kept, not just the fact that one arrived. Re-asking the same address for
    // data we already hold would waste a second round trip on the common path.
    size_t n = 0;
    m_last   = BatteryResult::NoReply;

    bool answered_direct = false;
    {
        const size_t got = query(kProbeId, rx, sizeof(rx));
        if (got >= 8) {
            BatteryReading      candidate;
            const BatteryResult r = parse(rx, got, candidate);
            if (r == BatteryResult::Ok || r == BatteryResult::Unsampled) {
                m_pid           = kProbeId;
                answered_direct = true;
                m_last          = r;
                n               = got;
                out             = candidate;
                LOGLN(F("   battery : pack answered at 0x01 — skipping provisioning"));
            }
        }
    }

    // Phase 1 runs only when the direct ask failed, i.e. the pack does not hold kProbeId.
    bool provisioned = answered_direct;
    if (!answered_direct) {
        provisioned = acquire_pid(rx, sizeof(rx));
        if (!provisioned) {
            LOGLN(F("   battery : no announcement — proceeding unprovisioned"));
        }
        link.flush(); // anything still queued from phase 1 is not part of the data reply
    }

    // There is deliberately no parameter-write phase here.
    //
    // A PARAMGET/PARAMSET pass used to sit at this point, on the hypothesis that the pack's
    // sensors sat at RULE_DISABLE and had to be armed before they would sample. That was
    // falsified from three directions: the pack's own announcement descriptors already report
    // rule 0x0008 (RULE_PERIODIC), the working reference reader never sends a parameter write
    // at all, and on this pack PARAMGET drew no reply while the "PARAMSET ack" turned out to
    // be an announcement arriving on its own schedule rather than a response to anything.
    //
    // It was left compiled-in behind FEATURE_BATTERY_PARAM_PASS for a while on the argument
    // that "the pack ignores this" is a claim about one pack. It has now been deleted, because
    // the real blocker turned out to be elsewhere entirely -- the pack will not accept an id
    // assigned over this link at all, and is provisioned out-of-band through WisToolBox
    // (docs/DEPLOY.md). A switched-off pass aimed at a falsified hypothesis is not a spare
    // tool; it is 210 lines every future reader has to understand before ruling out.
    //
    // CITE(prior-art): [CIT-MESHTASTIC-9154] @ 02050a4 variants/rak2560/RAK9154Sensor.cpp --
    //   the working reference never calls set.param or get.param. After ADD_PID it does
    //   nothing but poll SENDAT, so a parameter write is not a precondition for sampling.
    // CITE(bench): docs/EVIDENCE.md -- the announcement descriptor tail reads rule 0x0008 on
    //   every sensor, and the PARAMGET sent to arm them drew no reply at all.

    // Phase 2: request the latest sensor data from the address the pack answers on.
    //
    // Two addresses are possible and which one is live depends on whether the handshake above
    // took effect: the assigned probe id once provisioned, 0xFF while not. Rather than assume,
    // try the preferred one and fall back, then remember whichever answered. The bench proved
    // a wrong choice here costs the entire reading — a SENDAT to 0x01 on an unprovisioned pack
    // draws total silence, which is indistinguishable from an unplugged cable.
    //
    // Which address "answered" is decided by whether a SENDAT frame came back, NOT by whether
    // bytes came back, and that distinction is a fix rather than a nicety. The pack announces
    // itself spontaneously and repeatedly, so a request sent to an address nothing is
    // listening on still routinely returns a non-empty buffer — it just contains the
    // announcement. The previous revision took any non-zero byte count as proof and would
    // latch m_pid onto the dead address for the rest of the node's life, turning a recoverable
    // mis-addressing into a permanent one.
    //
    // CITE(bench): docs/EVIDENCE.md — dest sweep on 3d3425d: 0x01/0x02/0x03 -> 0 bytes,
    //   0xFF -> a full 28-byte SENDAT response with a valid checksum, immediately followed in
    //   the same read by the 92-byte spontaneous announcement. Bytes arriving and the request
    //   being answered are demonstrably different events on this bus.
    // CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.c api_process()
    //   dispatches on hub_api->type per frame, never on "something arrived"; verify_action()
    //   then routes SNHUB_TYPE_SENDAT to its own program. Frame type is the unit of meaning.
    //
    // Skipped entirely when phase 0 already got an answer from kProbeId: the address is
    // settled and the reply is already in hand, so repeating the exchange would buy nothing
    // but another round trip on the path that runs every hour for years.
    const uint8_t candidates[2] = {m_pid, (m_pid == kBroadcastId) ? kProbeId : kBroadcastId};

    for (uint8_t c = 0; c < 2 && !answered_direct; c++) {
        const size_t got = query(candidates[c], rx, sizeof(rx));
        if (got == 0) {
            continue;
        }
        n = got;
        if (got < 8) {
            if (m_last == BatteryResult::NoReply) {
                m_last = BatteryResult::ShortFrame;
            }
            continue;
        }

        BatteryReading      candidate;
        const BatteryResult r = parse(rx, got, candidate);

        // Ok and Unsampled both mean a SENDAT frame addressed to us came back from this
        // destination — one with data, one with placeholders. Either way the address is
        // right, and it is worth remembering.
        if (r == BatteryResult::Ok || r == BatteryResult::Unsampled) {
            m_pid  = candidates[c];
            m_last = r;
            out    = candidate;
            break;
        }
        if (m_last == BatteryResult::NoReply || m_last == BatteryResult::ShortFrame) {
            m_last = r;
        }
    }

    // Phase 2b: listen for the pack to push its own data, without asking again.
    //
    // This is the path the working reference actually reads from, and missing it is the best
    // explanation for the placeholder zeros. Meshtastic requests data exactly once — the
    // instant provisioning completes — and then sets `provision = 0` so it never requests
    // again. Every value it reports from then on arrives as an *unsolicited* SENDAT frame the
    // pack sends on its own schedule (RUI3 flag = REQ, dispatched through
    // protocol_list[SENDAT].req -> SNHUBAPI_EVT_REPORT). The solicited reply we poll for goes
    // down the other branch entirely (flag = RSP -> SNHUBAPI_EVT_SDATA_REQ).
    //
    // So the poll is a kick, and the push is the measurement. A freshly provisioned pack that
    // has not sampled yet answers the kick immediately with a well-formed record template —
    // exactly the all-zero frame the bench captured — and sends the real numbers moments
    // later, to a driver that by then has stopped listening.
    //
    // Only entered when the poll did not already produce a reading, so a pack that answers
    // properly pays nothing for this. Bounded by the same first-byte timeout as every other
    // receive, and nothing is transmitted: the line is simply left open.
    //
    // CITE(prior-art): [CIT-MESHTASTIC-9154] @ 02050a4 variants/rak2560/RAK9154Sensor.cpp —
    //   onewireHandle(): `if (provision != 0) { RakSNHub_Protocl_API.get.data(provision);
    //   provision = 0; }` then an unconditional `while (mySerial.available())` drain on every
    //   50 ms tick. One request in the device's lifetime; continuous listening thereafter.
    // CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.c
    //   protocol_list[SNHUB_TYPE_SENDAT] = { .req = snhub_snsrdat_req_program, .rsp =
    //   snhub_snsrdat_rsp_program }; the .req half raises SNHUBAPI_EVT_REPORT and exists only
    //   to handle a SENDAT the master never asked for. An unsolicited data push is a
    //   first-class part of this protocol, not an artefact.
    // CITE(bench): docs/EVIDENCE.md — passive listen at 9600 on 3d3425d, transmitting nothing,
    //   received the pack's spontaneous PROVISION announcement every cycle. The pack is
    //   demonstrably willing to talk unprompted on this wire.
    if (m_last != BatteryResult::Ok) {
        const size_t pushed = receive(rx, sizeof(rx), /*stop_on_provision=*/false,
                                      kPushListenUs);
        if (pushed >= 8) {
            BatteryReading      candidate;
            const BatteryResult r = parse(rx, pushed, candidate);
            if (r == BatteryResult::Ok) {
                LOGLN(F("   battery : live values arrived as an unsolicited report"));
                out    = candidate;
                m_last = r;
                n      = pushed;
            } else if (m_last == BatteryResult::NoReply) {
                m_last = r;
                n      = pushed;
            }
        }
    }

    // A pack that has once produced a real measurement never needs configuring again, and
    // saying so here is what stops the enable pass from running on every future wake.
    if (m_last == BatteryResult::Ok && !m_ever_sampled) {
        m_ever_sampled = true;
        LOGLN(F("   battery : sampling confirmed — pack is reporting live values"));
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

    // Sign is taken from the whole value, never from the integer part.
    //
    // The obvious formulation — `"%+d.%02u", v / 100, abs(v % 100)` — silently inverts every
    // current between -0.99 A and -0.01 A. For v = -50, `v / 100` truncates toward zero to 0,
    // which `%+d` renders as "+0", and `abs(v % 100)` supplies 50: a pack discharging at half
    // an amp prints as `+0.50 A`. Temperatures from -0.9 to -0.1 C had the same defect.
    //
    // That band is not an edge case. It is where a pack sits on an overcast day and under a
    // light load, which is exactly the condition an operator watches while settling the sign
    // convention — and this is the log they read to settle it (ADR-0002, issue #3). A wrong
    // sign here would make the wrong convention look confirmed, and that convention then gets
    // frozen into the payload schema and the TTN decoder, where unwinding it is expensive.
    //
    // Splitting sign from magnitude before dividing removes the whole class: int16_t widens
    // to int32_t without overflow, so negating the minimum is defined.
    LOG(F("   battery : "));
    if (out.voltage.valid) {
        LOGF("%u.%02u V  ", out.voltage.value / 100, out.voltage.value % 100);
    }
    if (out.current.valid) {
        const int32_t  v   = out.current.value;
        const uint32_t mag = (uint32_t)(v < 0 ? -v : v);
        LOGF("%c%lu.%02lu A  ", (v < 0) ? '-' : '+', (unsigned long)(mag / 100),
             (unsigned long)(mag % 100));
    }
    if (out.soc.valid) {
        LOGF("%u%%  ", out.soc.value);
    }
    if (out.temperature.valid) {
        const int32_t  t   = out.temperature.value;
        const uint32_t mag = (uint32_t)(t < 0 ? -t : t);
        LOGF("%s%lu.%lu C", (t < 0) ? "-" : "", (unsigned long)(mag / 10),
             (unsigned long)(mag % 10));
    }
    LOGLN("");

    return out;
}

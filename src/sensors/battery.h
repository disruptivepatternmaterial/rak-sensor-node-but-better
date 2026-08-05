/*
 * RAK9154 Solar Battery Lite — pack voltage, current, charge, and temperature.
 *
 * Talks the RAK Sensor Hub single-wire protocol on the 5-pin socket: a half-duplex 9600
 * 8N1 UART on one open-drain line, carrying IPSO type-length-value records. This is the
 * link the RAK2560 Sensor Hub normally uses to read the pack, and the cable for it ships
 * in the box.
 *
 * CITE(prior-art): [CIT-ONEWIRE-SERIAL] beegee-tokyo/RAK-OneWireSerial — the wake byte
 *   0xFF, frame delimiter 0x7E, and the IPSO type table (values are the IPSO object id
 *   minus a 3200 offset, which is what makes them fit in one byte).
 * CITE(prior-art): [CIT-MESHTASTIC-9154] Meshtastic's RAK9154Sensor — the battery probe
 *   enumerates as probe id 0x01, with the master as 0x00.
 * CITE(sibling): [CIT-RAK45WIRE] rak-4-5-wire/firmware/nanoc6-onewire-poll @ efc0e3c —
 *   clean-room
 *   codec this one follows for framing. It decoded only three TLV types; this
 *   implementation adds temperature, which the protocol carries as type 103 and that codec
 *   silently discarded as unrecognized. Its bit timing is NOT followed: that codec runs on
 *   an ESP32-C6, where a bit-banged pin write is cheap enough to work. On the nRF52840 it
 *   is not — see the transport rationale at the top of battery.cpp.
 */

#pragma once

#include "../build_features.h"
#include "../reading.h"

#include <stddef.h>
#include <stdint.h>

// How many 0xFF wake bytes lead every frame sent on the one-wire link.
//
// Exposed here, rather than kept private to battery.cpp, because src/diagnostics/owscan.cpp
// must send the same run: the pack has only ever replied to frames led by four, so a scan that
// sends one reports "no reply" for a working pack and sends the next reader after a fault that
// does not exist. That already happened. Everything else in owscan.cpp is deliberately
// duplicated so the scan can probe frames the driver never sends — this value is the exception,
// because it is not a protocol field, it is a physical-layer property of this pack, and there is
// no version of "correct" that differs between the two.
//
// CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.h — RUI3_Api_t carries
//   a single `U8 wakeup`, so four is a deliberate deviation from the reference struct.
// CITE(bench): docs/EVIDENCE.md — every SENDAT reply this pack has ever produced followed four
//   wake bytes; the revisions that sent one drew silence.
constexpr uint8_t kBatteryWakeCount = 4;

enum class BatteryResult : uint8_t {
    Ok = 0,
    NoReply,     // silent line — unplugged, or the pack's BMS is asleep
    ShortFrame,  // saw bytes but never a complete frame
    BadFrame,    // no delimiter, or a declared length that does not fit what arrived
    BadChecksum, // arrived intact-looking but does not verify — treated as no data
    NoRecords,   // frame verified but held nothing we understand
    // Frame verified, records understood, and every one of them read zero. Distinct from
    // NoRecords because it is a live pack answering with placeholders rather than a parsing
    // failure — and distinct from Ok because 0.00 V from a pack that is powered by the cell
    // it is measuring is not a measurement. Reported as no data; never encoded.
    Unsampled,

    // A frame arrived whose declared RUI3 length is longer than the bytes that actually
    // landed. This is a transport fault, not a data fault, and conflating the two cost days.
    //
    // The pack's provisioning announcement is 92 bytes. A read that returns 28 of them looks,
    // to a scanner that only asks "did any frame verify", exactly like a line with no frame on
    // it — so it was reported as BadFrame, alongside genuine noise, and the trailing bytes of
    // our own uninitialised buffer were then available to be walked as if they were records.
    // Reported separately so the console says "declared 92, got 28" and the next reader looks
    // at the receive path instead of at the protocol.
    Truncated,

    // A complete, checksum-valid PROVISION announcement arrived where a SENDAT reply was
    // expected, and no SENDAT frame arrived at all.
    //
    // This is the pack's actual observed behaviour and it deserves its own name. It means the
    // pack is alive, framing correctly, and talking — but it is announcing itself rather than
    // answering the question we asked. Previously indistinguishable from a corrupt frame.
    ProvisionOnly,
};

const char *battery_result_name(BatteryResult r);

class Battery {
  public:
    // `pin` is the data line. In the default build that is the single half-duplex wire, with
    // the pack's TXD and RXD bridged onto it. Under FEATURE_ONEWIRE_SPLIT it becomes the TX
    // line only — our output to the pack's RXD (socket pin 5) — and the pack's TXD (socket
    // pin 3) is read on a second pin fixed in battery.cpp, because that pin is a property of
    // the base board's solder pads rather than of the wiring harness. See
    // src/build_features.h for why both topologies exist and which physical change each one
    // expects.
    //
    // Held as configuration rather than a compile time constant so the wiring can move
    // without touching the protocol code.
    explicit Battery(uint8_t pin) : m_pin(pin) {}

    BatteryReading read();

    BatteryResult last_result() const { return m_last; }

  private:
    // `payload` is the SensorHub payload behind the six-byte header: empty for BOOT and
    // SENDAT, one sid byte for PARAMGET, a 43-byte parameter block for PARAMSET. Both the
    // transport length and the header's payload_length are derived from it, because the pack
    // cross-checks the two.
    void   send_frame(uint8_t dest, uint8_t hub_type, uint8_t payload_type,
                      const uint8_t *payload = nullptr, size_t payload_len = 0);
    void send_boot();

    // Drain the RX queue into `buf`. With `stop_on_provision` the drain returns the instant a
    // complete, checksum-verified PROVISION request is buffered instead of waiting out the
    // inter-byte gap — the pack's provisioning window is short and that gap is spent inside it.
    // Off by default, and deliberately not applied to the data path: the pack concatenates its
    // spontaneous announcement behind a SENDAT reply, so stopping at the first complete frame
    // there would throw the second one away.
    // `first_byte_timeout_us` of 0 means "use the normal short window". The push-listen path
    // passes a much longer one: the pack samples on its own periodic schedule, so waiting only
    // half a second for an unsolicited report is waiting for a coincidence.
    size_t receive(uint8_t *buf, size_t cap, bool stop_on_provision = false,
                   uint32_t first_byte_timeout_us = 0);

    // Hex-dump under a label, for the paths where the decoded verdict is not enough evidence.
    void dump(const char *what, const uint8_t *buf, size_t len);

    // BOOT once, then keep answering every announcement until a wall-clock deadline — the
    // reference master's steady state, approximated. Exits early the moment the pack proves it
    // latched the assigned id. Returns true when at least one announcement was answered this
    // cycle, which is a weaker claim than "the pack is provisioned": ask m_pack_latched for
    // that.
    bool acquire_pid(uint8_t *buf, size_t cap);

    // One "send SENDAT to `dest`, collect whatever comes back" round trip.
    size_t query(uint8_t dest, uint8_t *buf, size_t cap);

    // Completes the provisioning handshake. The pack announces itself unbidden with a
    // PROVISION request carrying provId = 0xFF; the master's job is to answer that
    // announcement with the same frame turned around and provId filled in. Rewrites `buf`
    // in place — the response is the request with five bytes changed — and returns true
    // when an announcement was found and answered.
    //
    // `announced_provid` reports the id the *pack* claimed, read before the mutation
    // overwrites it, and is set to 0xFF when no announcement was found. It is the only direct
    // evidence of whether a previous answer stuck: 0xFF means still unprovisioned, anything
    // else means the pack accepted an assignment.
    bool provision(uint8_t *buf, size_t len, uint8_t &announced_provid);

    // Byte-level transport. The bit timing lives in beegee-tokyo/RAK-OneWireSerial, not
    // here — see the rationale at the top of battery.cpp. Deliberately not a member of this
    // class: the library's type is nRF-only, and every other sensor header in this tree
    // stays free of Arduino headers so the off-target tests keep building.
    void tx_byte(uint8_t b);

    // Verifies the frame before believing any of it, then fills `out`. Returns the reason
    // when it refuses. Validation matters more here than it looks: an unverified record
    // scan will happily turn line noise into a plausible pack voltage, and a wrong battery
    // reading is worse than none — it is the number a human uses to decide whether the
    // node needs rescuing.
    BatteryResult parse(const uint8_t *buf, size_t len, BatteryReading &out);

    uint8_t m_pin;
    uint8_t m_seq = 0; // per-request sequence, mirrors menu.seq in the reference

    // The address the pack actually answers on. Starts at PID_UNKNOW (0xFF) because that is
    // where an un-provisioned pack listens — confirmed on the bench, where a SENDAT to 0x01
    // drew silence and the same frame to 0xFF drew a full reply. Upgraded to the assigned
    // probe id once the provisioning handshake completes, and thereafter sticky, so the
    // steady state costs one request per cycle.
    uint8_t m_pid = 0xFF;

    // Set once an announcement has been seen carrying a provId other than 0xFF — i.e. the pack
    // has confirmed, in its own words, that it accepted the id this master assigned. Distinct
    // from "we answered an announcement", which every previous revision could claim while the
    // pack went on reporting 0xFF forever. Nothing depends on it yet beyond ending the
    // provisioning window early and choosing the data-poll address; it exists so the one fact
    // that has cost the most bench time is held explicitly instead of re-derived from hex.
    bool m_pack_latched = false;

    // Set once the pack has produced a genuine measurement. Sampling is a persistent setting
    // on the pack, so a pack that has reported real values does not need configuring again —
    // this is what keeps the enable pass off the critical path for the rest of the node's
    // deployment rather than repeating it on every wake for months.
    bool m_ever_sampled = false;

    BatteryResult m_last = BatteryResult::NoReply;
};

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

#include "../reading.h"

#include <stddef.h>
#include <stdint.h>

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
};

const char *battery_result_name(BatteryResult r);

class Battery {
  public:
    // `pin` is the single bridged data line. Held as configuration rather than a compile
    // time constant so the wiring can move without touching the protocol code.
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

    // Read one sensor's sampling rule and interval. Read-only: this is how the driver finds
    // out whether the pack's sensors are armed (RULE_PERIODIC) or idle (RULE_DISABLE) without
    // changing anything. `dest` is passed explicitly rather than taken from a member so a
    // parameter exchange can never be sent to an address the data path merely fell back to.
    bool param_get(uint8_t dest, uint8_t sid, uint32_t &intv, uint16_t &rule, uint8_t *buf,
                   size_t cap);

    // Switch one sensor to periodic sampling at `intv` seconds, leaving thresholds and tag
    // zeroed as the reference does. The unit and the valid 60..86400 range come from RAK's
    // own WisToolBox command catalogue, so `intv` is clamped into that range here rather than
    // written through unchecked. Repeats on RAK's own budget — three attempts, three seconds
    // each — and dumps whatever comes back, because a parameter write is acknowledged in
    // RAK's tooling and the previous ~5 ms drain could never have observed the acknowledgement.
    bool param_set(uint8_t dest, uint8_t sid, uint32_t intv, uint8_t *buf, size_t cap);

    // Best-effort pass over every known sensor id. Cannot make a reading worse: a refused or
    // ignored request leaves the pack as it was, and the caller re-reads and re-applies the
    // all-zero test, so a pack that will not sample still yields nulls.
    bool enable_sampling(uint8_t *buf, size_t cap);

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

    // The id this master actually *assigned*, which is a different fact from the one above
    // and must be stored separately.
    //
    // `m_pid` is allowed to fall back to 0xFF whenever the assigned id does not answer a
    // SENDAT, because a reading from the broadcast address is still a reading. A parameter
    // write has no such latitude: it must go to the id the probe was given, or it addresses
    // a parameter record that does not exist. Holding both in one variable meant the data
    // path's fallback silently disarmed the enable pass — the driver logged "provisioned
    // probe 0xFF as pid 0x01" and "not provisioned — skipping sampling enable" in the same
    // cycle, and the one write that could start the pack sampling was never sent.
    //
    // Stays 0xFF until a provisioning handshake completes, and is never written by the read
    // path.
    uint8_t m_assigned_pid = 0xFF;

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

    // Sensor ids the pack announced, learned from the provisioning announcement's descriptor
    // tail. Empty until an announcement is parsed, in which case the enable pass falls back to
    // the four ids the pack's own data records carry.
    uint8_t m_sids[8]     = {0};
    uint8_t m_sid_count   = 0;

    // Wake cycles that have spent time trying to enable sampling. Bounded so a pack that
    // ignores the request stops costing a parameter exchange on every cycle for months.
    uint8_t m_enable_attempts = 0;

    BatteryResult m_last = BatteryResult::NoReply;
};

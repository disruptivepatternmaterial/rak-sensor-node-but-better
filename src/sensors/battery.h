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
// Exposed here rather than hidden in the .cpp because of what the value does: the pack has only
// ever replied to frames led by four wake bytes, so a reader that sends one gets "no reply" from a
// working pack and goes hunting a fault that does not exist. That already happened once. It is a
// physical-layer property of this pack, not a protocol field, and it is worth keeping visible so
// nobody re-derives it by driving the wire — which is not licence to write a scanner that does.
// See build_features.h.
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

    // A structurally valid SENDAT frame arrived, but it was not an answer to the request this
    // driver had outstanding: wrong flag, wrong destination, wrong source, or a sequence that
    // does not match the one sent.
    //
    // Distinct from BadFrame because nothing is wrong with the frame — it is somebody else's,
    // or the pack's own spontaneous push arriving inside a window where a solicited reply was
    // expected. Accepting such a frame lets a spontaneous report falsely validate the address
    // the driver just queried, which is issue #36.
    Unmatched,

    // A complete, checksum-valid PROVISION announcement arrived where a SENDAT reply was
    // expected, and no SENDAT frame arrived at all.
    //
    // This is the pack's actual observed behaviour and it deserves its own name, because a
    // corrupt frame calls for a different move: the pack is alive, framing correctly, and
    // talking — it is announcing itself rather than answering the question we asked.
    ProvisionOnly,
};

const char *battery_result_name(BatteryResult r);

// Declared rather than included: battery_frame.h includes this header, and the brownout gate
// lives behind an Arduino-dependent translation unit. Both are only ever dereferenced inside
// battery.cpp.
struct BatteryQueryMatch;
namespace power {
class Brownout;
}

class Battery {
  public:
    // `pin` is the data line: the single half-duplex wire, with the pack's TXD and RXD bridged
    // onto it. There is only one topology — the two-pin split was removed rather than left
    // switched off, and battery.cpp records why.
    //
    // Held as configuration rather than a compile time constant so the wiring can move
    // without touching the protocol code.
    explicit Battery(uint8_t pin) : m_pin(pin) {}

    BatteryReading read();

    BatteryResult last_result() const { return m_last; }

    // Hand the driver the node's brownout gate so it can decline to go hunting for a pack that
    // is not talking while the pack is also too low to spend energy on.
    //
    // Injected rather than reached for, because the instance lives in src/main.cpp and this
    // class must stay linkable in the off-target tests, which have no power subsystem. Null
    // until wired, in which case the ladder runs exactly as it did before — a missing gate
    // must never be read as "brownout engaged" and silently stop the battery from being read
    // at all. See issue #39.
    void set_brownout(const power::Brownout *b) { m_brownout = b; }

  private:
    // `payload` is the SensorHub payload behind the six-byte header: empty for BOOT and
    // SENDAT, one sid byte for PARAMGET, a 43-byte parameter block for PARAMSET. Both the
    // transport length and the header's payload_length are derived from it, because the pack
    // cross-checks the two.
    void   send_frame(uint8_t dest, uint8_t hub_type, uint8_t payload_type,
                      const uint8_t *payload = nullptr, size_t payload_len = 0);
    void send_boot();

    // One BOOT per *failure episode*, sent only once the pack has been silent at its assigned
    // id for kSilentCyclesBeforeBoot consecutive cycles, and never twice inside
    // kBootMinSpacingCycles. BOOT is the reference's reboot verb; sending it on every re-latch
    // attempt rebooted the pack mid-handshake. An episode ends at the next genuine reading,
    // which re-arms the allowance. See boot_if_warranted() in battery.cpp and issues #62, #71
    // and #75.
    void boot_if_warranted();

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

    // Keep answering every announcement until a wall-clock deadline — the reference master's
    // steady state, approximated. Sends nothing to open the window: the pack announces itself
    // unprompted, and a BOOT here reboots it instead (issue #62). Exits early the moment the
    // pack proves it latched the assigned id, and returns true only in that case — answering an
    // announcement is not provisioning and must not be read as it.
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
    // when it refuses.
    //
    // The decoding itself lives in battery_frame.cpp, which has no Arduino dependency and is
    // therefore covered by host tests (issue #42). This wrapper exists to print what the codec
    // observed — the codec cannot log — and to supply the match criteria that tie a reply to
    // the request that asked for it (issue #36).
    BatteryResult parse(const uint8_t *buf, size_t len, BatteryReading &out,
                        const BatteryQueryMatch &match);

    // May this cycle pay for the announcement window and the push listen, or is it a cheap
    // probe only? See issue #39 and the constants in battery.cpp.
    bool ladder_allowed();

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
    // from "we answered an announcement", which can be true while the pack goes on reporting
    // 0xFF forever. Nothing depends on it beyond ending the provisioning window early and
    // choosing the data-poll address; it exists so the one fact that has cost the most bench
    // time is held explicitly instead of re-derived from hex.
    //
    // Must be cleared again whenever an answered announcement still carries provId 0xFF. Left
    // write-once, one good latch makes every later failure print as a success for the rest of
    // the deployment — issue #62.
    bool m_pack_latched = false;

    // Whether the current failure episode has already spent its one BOOT.
    //
    // Cleared by a genuine reading, because an answering pack ends the episode: a later failure
    // is a new one and deserves its own nudge. Scoped per episode and not per *power cycle*,
    // which on the field image is months — that scope spent the deployment's only BOOT on the
    // first transient probe miss (issue #75) and left a pack that went mute later with no nudge
    // (issue #71). m_next_boot_cycle is the bound that keeps "per episode" from becoming
    // "often". See boot_if_warranted().
    bool m_boot_spent = false;

    // Earliest cycle at which another BOOT may be sent, whatever the episode bookkeeping says.
    //
    // Deliberately *not* cleared by a reading. m_boot_spent alone would let a pack that
    // alternates a short silence streak with one good reading draw a BOOT every few cycles, and
    // a periodic reboot of a nearly-working pack is the failure #62 is about. This is the hard
    // floor underneath the episode rule. Compared against m_cycles rather than millis() so it
    // does not depend on the wall clock surviving sleep.
    uint32_t m_next_boot_cycle = 0;

    // Set once the pack has produced a genuine measurement, and used for exactly one thing:
    // announcing that fact to the console a single time instead of on every wake for months.
    // Nothing else gates on it — it is not evidence that a pack configuration path exists.
    bool m_ever_sampled = false;

    BatteryResult m_last = BatteryResult::NoReply;

    // The brownout gate, or null when nothing has wired one in. See set_brownout().
    const power::Brownout *m_brownout = nullptr;

    // Consecutive cycles in which the pack produced no reading at all.
    //
    // The happy path is cheap: the pack answers the direct query in well under a second. The
    // expensive part — the 5 s announcement window and the 20 s push listen — only runs when
    // that fails, so a pack that has stopped talking costs roughly 28 s of wake time every
    // cycle, forever, with nothing to show for it. Counting the failures is what turns that
    // into a cheap probe plus an occasional full retry. See issue #39.
    uint16_t m_silent_cycles = 0;

    // Consecutive cycles in which the pack answered with a checksum-valid but empty record.
    //
    // Kept apart from m_silent_cycles because the two are different evidence and earn
    // different allowances: an empty record proves the pack is present and framing, and the
    // push listen is the only thing that turns it into a reading, so it is worth paying for
    // longer. Both feed the same bound in ladder_allowed(), so neither can run the expensive
    // phases forever.
    uint16_t m_unsampled_cycles = 0;

    // Every cycle since the last real reading that produced no reading, of either kind.
    //
    // The two counters above are streaks and each resets when the other kind of failure
    // occurs, so neither bounds a pack that alternates between them. This one resets only on
    // a genuine measurement and is what stops the ~28 s expensive phases running every cycle
    // forever in that case. See kStalledCyclesBeforeProbeOnly in battery.cpp.
    uint16_t m_stalled_cycles = 0;

    // Cycle count at which the full ladder is next allowed to run again, once the driver has
    // dropped back to probe-only. Compared against m_cycles rather than millis() so it does
    // not depend on the wall clock surviving sleep.
    uint32_t m_cycles          = 0;
    uint32_t m_next_full_cycle = 0;
};

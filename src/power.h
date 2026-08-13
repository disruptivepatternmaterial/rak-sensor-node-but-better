/*
 * Watchdog and sleep.
 *
 * Two jobs, both of which decide whether the node is still reporting in March.
 *
 * The watchdog guards the awake part of the cycle only. On this chip the watchdog cannot
 * be stopped once started and its timeout is fixed at start time, so a timeout long enough
 * to span an hour of sleep would be uselessly long for catching a stuck sensor read. The
 * way out is a configuration bit that pauses the counter while the CPU sleeps: the timer
 * then measures awake time, which is exactly the window where a hang can happen.
 *
 * Sleep has to actually be sleep. Powering the CPU down does not power anything else down
 * — the radio keeps its own state, and any UART or SPI left enabled keeps its clock
 * running. Each of those costs around a milliamp, which over an hourly cycle dwarfs
 * everything the node does while awake.
 *
 * CITE(datasheet): [CIT-NRF-WDT] the watchdog's registers lock on start and only a reset
 *   clears them; timeout is (CRV + 1) / 32768 seconds; CONFIG.SLEEP chooses whether the
 *   counter runs while the CPU is asleep.
 * CITE(datasheet): [CIT-NRF-POWER] sleep modes do not disable peripherals automatically.
 * CITE(prior-art): [CIT-RAK-SLEEP] the WisBlock API author's account of the two failures
 *   this file exists to avoid: sleeping the MCU without also sleeping the SX1262, and a
 *   node that cannot join never sleeping at all. A reporter measured 6 mA in that state.
 */

#pragma once

#include <stdint.h>

namespace power {

// Starts the watchdog with a timeout covering the longest legitimate awake period. Once
// this returns the watchdog is running and cannot be turned off — only fed or reset.
void watchdog_begin(uint32_t timeout_seconds);

// Feeds the watchdog. Call at points that prove real progress, not on a timer: a feed
// inside a loop that is itself stuck defeats the entire mechanism.
void watchdog_feed();

// True when the last reset came from the watchdog rather than power-on. Worth surfacing,
// because a node quietly resetting every cycle otherwise looks perfectly healthy.
bool reset_was_watchdog();

// Shuts down peripherals, then sleeps for the requested time. Returns once the interval
// has elapsed and the console is usable again.
void sleep_seconds(uint32_t seconds);

// Pack voltage thresholds, in hundredths of a volt to match what the pack reports.
//
// The pack is nominally 10.8 V, which is three lithium cells in series. Full is about
// 12.6 V, empty is about 9.0 V, and the pack's own protection circuit disconnects
// somewhere below that. These numbers are set to keep the node clear of that disconnect,
// not to squeeze out the last uplink.
//
// The reasoning is asymmetric on purpose. Missing a day of readings is an inconvenience.
// Letting the pack reach protection cutoff is a hike, because a disconnected pack may not
// restart from panel current alone — and a transmit burst is the largest current this node
// ever draws, so it is the most likely thing to drag a tired pack over that edge.
//
// ASSUMPTION: cell chemistry and count are inferred from the 10.8 V nominal rating, and the
// pack's actual cutoff has not been measured. Confirm both, then revisit. Recorded as TBD
// in docs/POWER_BUDGET.md.
constexpr uint16_t kTxInhibitCentivolts = 960;  // 3.2 V per cell — stop transmitting
constexpr uint16_t kTxResumeCentivolts  = 1020; // 3.4 V per cell — and start again

// Consecutive unreadable pack voltages after which the node stops transmitting anyway.
//
// This is the answer to "what does no evidence mean," and it has to be answered explicitly
// because the two obvious readings point opposite ways. Holding the previous decision is
// right once the gate is already engaged. It is wrong from a cold boot, where the previous
// decision is the default — and defaulting to transmit-allowed is what made this gate fail
// open: a reset returned the node to transmitting, and a pack that had gone silent because
// it was low left no evidence to correct it.
//
// So: no evidence for this many cycles is treated as bad evidence, and the node goes quiet.
// The trade is deliberate and asymmetric in the same direction as the thresholds above. A
// node silenced by a dead one-wire link loses data, which costs a hike to fix but only
// once. A node transmitting blind into a sagging pack reaches protection cutoff, which may
// not restart on panel current at all.
//
// Four, not one or two, because the pack does not answer for roughly the first two cycles
// after a boot while it takes its own samples — engaging on that would silence every
// healthy node for one recovery cycle after every reset. Four clears the observed startup
// silence with margin and still reacts inside four intervals.
//
// CITE(bench): docs/EVIDENCE.md 2026-08-05 — the RAK9154 returns nothing for about the
//   first two cycles after boot before it latches and begins reporting, which is the
//   measurement that sets the floor under this count.
// CITE(policy): docs/POWER_BUDGET.md — never let the pack reach a state it cannot recover
//   from by itself, which is the rule that decides which way this fails.
constexpr uint8_t kInvalidReadsBeforeInhibit = 4;

// Cycles the node stays silent on the no-evidence path before it transmits anyway.
//
// The gate above closed a fail-open hole and opened a mirror one: the only voltage evidence
// this node has comes from the RAK9154 one-wire link, so a broken wire or connector silences
// a node whose pack is full, and it stays silent forever. Class A means a downlink can only
// follow an uplink, so a permanently quiet node is also an uncommandable one — there is no
// route left to tell it otherwise. Silent-forever and drained both end in a hike, and the
// silent one gives no warning first.
//
// There is no second voltage source to cross-check against. The pack's P+ reaches this board
// through a 12 V→5 V buck and is explicitly kept off the base board's `BAT` connector, which
// is the only thing the base board's battery divider observes — so the chip's ADC cannot see
// pack voltage at all, at any scaling. Inventing a mapping from the 3V3 rail back to the pack
// would be a fabricated reading, which is worse than no reading. See ADR-0007.
//
// So the hold is bounded instead of absolute: after this many consecutive no-evidence cycles
// the node sends one uplink regardless, then resumes holding for another full interval. That
// keeps the node distinguishable from a dead one and keeps the downlink path reachable.
//
// This applies **only** to the no-evidence path. When the pack answers and reports a low
// voltage, staying quiet is exactly right and there is no keepalive — the evidence says
// spending energy is the wrong move, and #38 exists because that used to be ignored.
//
// Twenty-four, so at the default hourly interval the node is heard from about once a day. One
// uplink per day is roughly 5% of the sandbox airtime allowance and a rounding error against
// a pack measured in amp-hours, which is what makes this affordable where transmitting every
// cycle blind into a sagging pack is not.
//
// CITE(policy): [CIT-TTN-FUP] — 30 s of uplink airtime per node per 24 h. A single short
//   uplink a day sits far inside that, so the keepalive cannot breach the fair-use budget
//   even when the node is otherwise silent for months.
// CITE(spec): docs/FIRMWARE_SPEC.md §2 — "P+/P− ... → 12 V→5 V buck → WisBlock 5 V. Never
//   feed P+ to `BAT`." This is the line that rules out an ADC cross-check and forces a
//   bounded silence rather than a second voltage source.
// CITE(bench): docs/EVIDENCE.md 2026-08-05 — the one-wire link is the element that has
//   actually failed repeatedly during bring-up, which is why single-sourcing the gate on it
//   is the risk worth bounding.
constexpr uint16_t kNoEvidenceKeepaliveCycles = 24;

// Called on a change of the gate's state so it can be remembered across a reset. Passed in
// rather than called directly, because persistence lives in Config and this file has no
// business knowing about a filesystem.
using BrownoutPersistFn = void (*)(bool engaged);

// Decides whether the node may transmit or write to flash, given what the pack reports.
//
// Separate thresholds for stopping and resuming, because a pack sitting exactly at the
// limit would otherwise transmit, sag below it, recover, and transmit again — burning the
// remaining energy on the oscillation itself.
class Brownout {
  public:
    // Restores the gate from whatever the last run persisted, and registers the sink used
    // to persist future changes. Call once, before the first update(). Without this the
    // gate starts disengaged, which is only correct on a build that has no battery driver
    // and therefore never calls update() at all.
    void begin(bool persisted_engaged, BrownoutPersistFn persist);

    // Feeds in the latest pack voltage. When the pack does not answer, the previous
    // decision stands — an unreadable battery is not evidence of a healthy one — but only
    // for kInvalidReadsBeforeInhibit cycles, after which the absence of evidence engages
    // the gate rather than leaving it open.
    void update(bool voltage_valid, uint16_t centivolts);

    // False when the pack is too low to spend energy on a transmission.
    bool transmit_allowed() const { return !m_engaged; }

    // False when a write could be interrupted by the pack sagging. A half-written
    // configuration or session file is worse than a stale one, because it survives the
    // reset and keeps breaking every boot afterwards.
    //
    // Stays false on the no-evidence path even on a keepalive cycle. Transmitting blind for
    // the sake of being heard from is a bounded risk; writing flash blind is not, and the
    // failure it produces survives every reset afterwards.
    //
    // One documented exception exists outside this gate, and it is not a leak in it: the frame
    // counter checkpoint behind an armed keepalive, authorized explicitly per uplink by
    // session::permit_counter_checkpoint() and refused for everything else. It is allowed
    // because the alternative is not a safer node but a permanently mute one — the counter
    // reserve is finite and neither hold that arms a keepalive can be lifted by anything the
    // node does. It is affordable because the checkpoint rides a cycle that is already
    // transmitting, and a LoRa burst is the largest current this node ever draws, so a page
    // write beside it is small against a decision already taken. A pack at or below
    // kTxInhibitCentivolts never reaches it at all, because power.cpp disarms the keepalive
    // there (#38). See session.h for the full contract.
    bool flash_write_allowed() const { return !m_engaged; }

    bool engaged() const { return m_engaged; }

    // True when the hold came from the absence of a reading rather than from a low one. The
    // two want opposite handling and the log line should not conflate them: one means the
    // pack is low, the other means nobody knows.
    bool engaged_without_evidence() const { return m_engaged && m_without_evidence; }

    // True when the node has been held silent long enough that it should transmit once anyway.
    //
    // Armed for the two holds that no action of the node's own can lift: one resting on no
    // reading at all, and one resting on a reading between the inhibit and resume thresholds.
    // Never armed for a reading at or below kTxInhibitCentivolts — that pack is genuinely too
    // low to spend energy, and #38 exists because a keepalive there used to be sent anyway.
    bool keepalive_due() const
    {
        return m_engaged && m_keepalive_armed && m_silent_cycles >= kNoEvidenceKeepaliveCycles;
    }

    // Called after a keepalive uplink actually reaches the air, so the count restarts. Split
    // from keepalive_due() on purpose: a join that fails must not consume the keepalive, or a
    // node with a dead pack link and a missing gateway would go another full interval quiet
    // for a transmission that never happened.
    void note_keepalive_sent();

  private:
    void set_engaged(bool engaged, bool persist);

    BrownoutPersistFn m_persist       = nullptr;
    bool              m_engaged       = false;
    uint8_t           m_invalid_reads = 0;

    // Whether the current hold rests on absence of evidence, and how many cycles it has
    // held. Neither is persisted: the no-evidence hold itself is not persisted either, since
    // it re-engages within kInvalidReadsBeforeInhibit cycles for the same reason it did the
    // first time.
    bool     m_without_evidence = false;
    uint16_t m_silent_cycles    = 0;

    // Whether the current hold is one the node cannot lift by itself, and therefore one whose
    // silence has to be bounded. Separate from m_without_evidence because a hold resting on a
    // reading inside the hysteresis band is equally inescapable and equally in need of a
    // keepalive, while reading as perfectly well-evidenced.
    bool m_keepalive_armed = false;
};

} // namespace power

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
    bool flash_write_allowed() const { return !m_engaged; }

    bool engaged() const { return m_engaged; }

  private:
    void set_engaged(bool engaged, bool persist);

    BrownoutPersistFn m_persist       = nullptr;
    bool              m_engaged       = false;
    uint8_t           m_invalid_reads = 0;
};

} // namespace power

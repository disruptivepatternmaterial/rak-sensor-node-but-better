/*
 * LoRaWAN session persistence.
 *
 * Joining a network produces a session: an address and two keys derived during the
 * handshake, plus a counter that increments with every uplink. Held only in RAM, all of it
 * is lost on any reset — a watchdog recovery, a brownout, a battery swap — and the node
 * must join again before it can send anything.
 *
 * That matters more than it sounds. A rejoin is not free: it needs the gateway to be
 * reachable at that moment, so a node that resets during an outage cannot report even
 * after the outage ends until a join happens to succeed. Worse, a node resetting in a loop
 * produces a stream of join requests, which is both the slowest way to spend the power
 * budget and unfriendly to a shared network.
 *
 * Saving the session turns a reset into a non-event: the node wakes up already joined and
 * sends its next reading.
 *
 * The counter needs care. It must never repeat — a replayed value is silently dropped by
 * the network, and the node looks alive while its data goes nowhere. Writing it on every
 * uplink would solve that and wear a flash page out in about a year at hourly reporting.
 * Instead it is saved periodically, always storing a value ahead of the real one, so after
 * a reset the counter resumes above anything already used. The cost is a gap in the
 * sequence, which the network tolerates by design.
 *
 * CITE(spec): [CIT-LW-LINK] the session state established by a join, and the frame counter
 *   rules that make a repeated value unacceptable.
 * CITE(prior-art): [CIT-SX126X-ARDUINO] the MIB get and set calls used to read the session
 *   out of the MAC and push it back in.
 * CITE(datasheet): [CIT-NRF-POWER] internal flash behavior behind the stored copy.
 */

#pragma once

#include <stdint.h>

namespace session {

// How many uplinks may pass between counter writes. Every save stores the current counter
// plus this many, so a reset resumes past any value that was actually used. Larger means
// fewer flash writes and a bigger gap after a reset; smaller means the opposite.
constexpr uint32_t kCounterMargin = 32;

// Returns true when writing flash is currently safe. Injected rather than queried directly
// because the answer lives in power::Brownout, which this module has no business knowing
// about — the same reason Brownout takes its persist callback instead of calling config.
using FlashWriteGateFn = bool (*)();

// Installs the gate. Until this is called, and in any build that never calls it, writes are
// allowed — an un-wired gate must not silently disable session persistence.
void set_flash_write_gate(FlashWriteGateFn gate);

// Pushes a stored session into the MAC and marks it joined. Returns false when there is
// nothing stored, when it is unreadable, or when it belongs to different firmware — in
// every case the caller should just join normally.
bool restore();

// Reads the live session out of the MAC and stores it. Call right after a successful join.
bool save();

// True when one more uplink can be sent without the frame counter reaching the value held in
// flash. Call before every transmission and do not transmit on false.
//
// The stored counter is the only thing standing between this node and a silent replay. A reset
// resumes from it, so every value actually put on the air has to stay strictly below it. When
// the live counter reaches it this advances the stored value and answers true; it answers false
// only when that write could not happen, which is what the H3 brownout gate does (#51).
//
// False must stop the uplink rather than merely be logged. Transmitting past the stored value
// produces frames a reset replays, and a replay is discarded by the network without any signal
// reaching the node — unconfirmed uplinks report success, so no failure counter moves and no
// rejoin is triggered. The node would look healthy and reach nobody for days.
//
// Cheap: one MIB read, and a flash write roughly once a month at the default interval.
bool counter_headroom_ok();

// Authorizes exactly one counter checkpoint write on the next headroom check, even while the
// brownout gate is withholding flash writes. Consumed by that check whether or not it needed it,
// so it can never carry over to a later uplink.
//
// This exists because the reserve is finite and the hold that consumes it need not end. A hold
// resting on a pack that has stopped answering is lifted only by a valid reading at or above the
// resume threshold, and the keepalive uplink is the only thing that keeps the node reachable
// meanwhile. Without this the reserve runs out after kCounterMargin keepalives and
// counter_headroom_ok() refuses every uplink afterwards: the node goes mute, and being Class A it
// is then also uncommandable, which is the state AGENTS.md says the node must never reach.
//
// Granted only for a keepalive the brownout gate itself armed — never for an ordinary uplink, and
// never on a pack measured at or below the transmit-inhibit floor. That last one is what #38 is
// about, and it is enforced where the arming happens rather than here: power.cpp disarms the
// keepalive outright for any reading at or below kTxInhibitCentivolts, so such a pack transmits
// nothing and therefore takes no write. Two holds can arm a keepalive, and both take this write:
// a hold resting on no reading at all, and a hold resting on a pack answering from inside the
// 9.60-10.20 V hysteresis band — above the floor, simply not recovered yet. Excluding the second
// would be worse than pointless: it is the hold that can last a whole winter, so the reserve
// would empty about eight days in and the node would go mute in exactly the scenario the in-band
// keepalive was added to prevent.
//
// Write frequency, since the earlier "roughly monthly" here was wrong at the cadence this node
// actually runs: one write per kCounterMargin = 32 keepalives, and during a hold a keepalive is
// one per kNoEvidenceKeepaliveCycles = 24 cycles, so 32 × 24 = 768 cycles — about 8 days at the
// 900 s field cadence and about 32 days at the 3600 s default. That is not the healthy path's
// rate but roughly a twenty-fourth of it: healthy, every uplink advances the counter, so a write
// falls every 32 cycles — about 8 hours at 900 s.
//
// CITE(spec): [CIT-LW-LINK] the frame counter rules that make a repeated value unacceptable, and
//   Class A's rule that a downlink can only follow an uplink — the two together are why a node
//   that stops transmitting to protect its counter can never be told to start again.
// CITE(prior-art): [CIT-LITTLEFS-DESIGN] "All POSIX operations, such as remove and rename, are
//   atomic, even in event of power-loss" — the property that makes this write safe to take on a
//   sagging supply: it commits or it does not, and a half-written record cannot be read back as
//   a valid one.
// CITE(policy): docs/POWER_BUDGET.md — never let the pack, or the node, reach a state it cannot
//   recover from by itself.
void permit_counter_checkpoint();

// Discards the stored session, so the next boot joins fresh. Used when the network has
// clearly stopped honoring the session.
void forget();

} // namespace session

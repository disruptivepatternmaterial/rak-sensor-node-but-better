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

// Stores an advanced frame counter if enough uplinks have passed since the last write.
// Cheap to call after every uplink; it usually does nothing.
void maybe_save_counter();

// Discards the stored session, so the next boot joins fresh. Used when the network has
// clearly stopped honoring the session.
void forget();

} // namespace session

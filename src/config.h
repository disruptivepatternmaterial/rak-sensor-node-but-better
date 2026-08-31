/*
 * Settings that survive a reset.
 *
 * Only the uplink interval lives here. Everything else is compiled in, on purpose: a
 * setting that can be changed remotely is also a setting that can be corrupted remotely,
 * and this node has to keep working with nobody nearby to undo a mistake.
 *
 * Stored in the nRF52840's internal flash through the Adafruit core's LittleFS. Writes are
 * rare by design — flash endures a bounded number of erase cycles per page, so writing on
 * every uplink would wear a page out inside a year. The interval is written only when it
 * actually changes.
 *
 * CITE(datasheet): [CIT-NRF-POWER] internal flash and RAM retention behavior across the
 *   sleep modes this node uses.
 * CITE(prior-art): [CIT-TINYUSB] the Adafruit nRF52 core that supplies InternalFileSystem.
 * CITE(spec): [CIT-TTN-FUP] the interval bounds exist to stay inside the network's fair
 *   use allowance, which is why an out-of-range value is refused rather than clamped
 *   silently.
 */

#pragma once

#include <stdint.h>

#include "build_features.h"

// A bench build that could also transmit is refused at compile time rather than guarded at
// run time. Sixty-second uplinks are roughly 1440 a day; at the slowest US915 rate that is
// well over an hour of airtime against an allowance of thirty seconds, so such a build has
// no legitimate use on the shared network and the only safe number of ways to produce one
// is zero. If bench-cadence radio testing is ever genuinely needed it belongs on a private
// network, where the allowance does not apply — see issue #26.
//
// CITE(policy): [CIT-TTN-FUP] the sandbox allowance is 30 s of uplink airtime per node per
//   24 h, and the same page states the limits do not apply on a private network.
#if FEATURE_BENCH_INTERVAL && FEATURE_RADIO
#error "FEATURE_BENCH_INTERVAL is bench-only and must not be built with FEATURE_RADIO — a 60 s uplink cadence breaches the TTN fair use allowance (docs/FIRMWARE_SPEC.md §4)."
#endif

// Bounds from docs/FIRMWARE_SPEC.md §4. The lower bound is a network-airtime limit rather
// than anything the hardware cares about — at any interval in this range the energy the
// node spends is irrelevant against the pack, so airtime is the only thing setting it.
//
// 900 s, lowered from 1800 s for 15-minute reporting. The allowance is about 30 seconds of
// transmit time per device per day. 900 s is 96 uplinks a day; at the slowest US915 rate
// (DR0, SF10BW125) an 11-15 byte uplink costs roughly 370 ms, so 96 of them is about 36
// seconds — over the allowance. At DR3 (SF7BW125) the same uplink is about 60 ms and the
// day totals under 6 seconds, comfortably inside it.
//
// So 900 s is compliant at DR3 or better and marginal at DR0. That is a coverage-dependent
// condition, not a fixed guarantee, and it is the reason this floor is not lower still:
// adaptive data rate settles a node with usable gateway coverage well above DR0, while a
// node at the edge of coverage stays there. The spec section records the trade explicitly.
//
// CITE(policy): [CIT-TTN-FUP] the sandbox allowance is 30 s of uplink airtime per node per
//   24 h — the figure the arithmetic above is measured against.
// CITE(spec): [CIT-LORA-RP002] US915 data rate to spreading factor mapping, which is what
//   turns a payload size into the per-uplink airtime used here.
// The shortest cadence any transmitting build may be capable of. Single-sourced here so the
// field floor and the compile-time assertion that defends it cannot drift apart — the bench
// build widens kIntervalMinSeconds below, so that constant cannot serve as the reference.
constexpr uint32_t kFupFloorSeconds = 900;

#if FEATURE_BENCH_INTERVAL
// Bench cadence. Only reachable with the radio compiled out, so no airtime is spent at all
// and the fair-use arithmetic above does not apply — the node reads the sensor and prints.
//
// Sixty seconds rather than something shorter because it is the number the bench actually
// needs: long enough that a full RK900 poll, including both retries at a 1000 ms timeout,
// finishes with margin, and short enough that a wiring change is confirmed or refuted while
// the operator's hand is still on the harness.
//
// CITE(spec): [CIT-MODBUS-SERIAL] the master's response timeout and retry behavior, which
//   is what sets the floor on how long one poll can take before the next is due.
// CITE(policy): [CIT-TTN-FUP] why this value may never reach a build that transmits.
#if FEATURE_BATTERY_FAST
// Battery bring-up cadence. Only reachable with the radio compiled out, and only from the
// battdiag environment, so no airtime is spent and the fair-use arithmetic above still does not
// apply. Ten seconds because the battery driver in that build finishes in about five: one
// Modbus attempt, a 3 s provisioning window and a 2 s push listen. Short enough that a
// hypothesis is confirmed or refuted while the operator is still watching the console.
constexpr uint32_t kIntervalMinSeconds     = 10;
constexpr uint32_t kIntervalMaxSeconds     = 86400;
constexpr uint32_t kIntervalDefaultSeconds = 10;
#else
constexpr uint32_t kIntervalMinSeconds     = 60;
constexpr uint32_t kIntervalMaxSeconds     = 86400;
constexpr uint32_t kIntervalDefaultSeconds = 60;
#endif
#else
constexpr uint32_t kIntervalMinSeconds     = kFupFloorSeconds;
constexpr uint32_t kIntervalMaxSeconds     = 86400;
constexpr uint32_t kIntervalDefaultSeconds = 3600;
#endif

// Ceiling on the between-cycle wait when sleep is compiled out, so bring-up is not spent
// watching a blank screen for an hour.
//
// This lives here rather than beside the loop that uses it because it is an interval policy,
// not a detail of the main loop: with sleep compiled out it *is* the reporting cadence, and
// the guard below has to be able to see it. That was the defect in issue #44 — the cap sat
// in main.cpp where no fair-use check could reach it, so `stage3` (sleep off, radio on)
// waited 30 s between uplinks, roughly 2880 a day, without touching FEATURE_BENCH_INTERVAL
// and without tripping the #error above.
//
// The cap does not apply when the radio is compiled in. A build that transmits has to honor
// the interval floor by whichever path produces it, and stage3's own comment in
// platformio.ini already said it was expected to run at the field floor. Capping the wait
// there was silently overriding the cadence the environment asked for.
#if FEATURE_RADIO
constexpr uint32_t kAwakeWaitCapSeconds = kIntervalMaxSeconds;
#elif FEATURE_BENCH_INTERVAL
// On a bench build the ceiling has to be at least the bench interval or it would silently
// override the cadence the operator asked for — a 30 s cap against a 60 s interval reads as
// the setting having been ignored.
constexpr uint32_t kAwakeWaitCapSeconds = kIntervalDefaultSeconds;
#else
constexpr uint32_t kAwakeWaitCapSeconds = 30;
#endif

// The shortest gap between uplinks this build can actually produce, by any route.
//
// Sleep on: the interval floor is the cadence. Sleep off: the awake wait is capped, so the
// cadence is whichever of the two is smaller. Deriving it rather than checking flags is the
// point — the previous guard enumerated one bad combination (bench interval plus radio) and
// a second, unenumerated one reached the same place. A combination nobody has thought of yet
// still has to pass through this expression.
#if FEATURE_SLEEP
constexpr uint32_t kEffectiveMinIntervalSeconds = kIntervalMinSeconds;
#else
constexpr uint32_t kEffectiveMinIntervalSeconds =
    (kIntervalMinSeconds < kAwakeWaitCapSeconds) ? kIntervalMinSeconds : kAwakeWaitCapSeconds;
#endif

// Any build that transmits must be capable of no cadence faster than the fair-use floor.
// Asserted rather than clamped at run time: a build that can breach the shared network's
// allowance has no legitimate use, so the number of ways to produce one should be zero.
//
// Set to the field floor itself, so the assertion cannot contradict the floor it is
// defending — lowering the floor lowers this with it, and the airtime reasoning for the
// value lives in one place above.
//
// CITE(policy): [CIT-TTN-FUP] the sandbox allowance is 30 s of uplink airtime per node per
//   24 h, and the same page states the limits do not apply on a private network — which is
//   why radio bench cadence testing belongs there instead of behind an exception here.
#if FEATURE_RADIO
static_assert(kEffectiveMinIntervalSeconds >= kFupFloorSeconds,
              "This build can transmit faster than the fair-use interval floor. Some "
              "combination of FEATURE_SLEEP, FEATURE_BENCH_INTERVAL and the awake-wait cap "
              "produces an effective uplink cadence below 900 s, which breaches the TTN "
              "fair use allowance (docs/FIRMWARE_SPEC.md §4). Either raise the effective "
              "interval or build with FEATURE_RADIO=0.");
#endif

class Config {
  public:
    // Mounts the filesystem and loads stored values. Any failure leaves the defaults in
    // place and reports it — an unreadable filesystem must not stop the node reporting.
    void begin();

    uint32_t interval_seconds() const { return m_interval; }

    // The one bounds test for a requested interval. Public and static because main.cpp has to
    // make the same judgement on a path that never reaches set_interval_seconds(): a
    // set-interval downlink taken during a brownout hold is applied in RAM and written to
    // flash cycles later, so Config never sees the value at the moment it has to be accepted
    // or refused. Two copies of a range check drift, and the copy that drifts is the one that
    // lets an out-of-range cadence onto a node nobody can walk up to.
    //
    // CITE(spec): docs/FIRMWARE_SPEC.md §4 — allowed interval 900-86400 s inclusive; this
    //   predicate is now the single implementation of that range.
    // CITE(policy): [CIT-TTN-FUP] 30 s of uplink airtime per node per 24 h — the allowance the
    //   lower bound defends, which makes a second drifting copy of the check a compliance
    //   defect rather than a tidiness one.
    // CITE(spec): [CIT-LW-LINK] Class A opens receive windows only after an uplink, so a
    //   cadence accepted wrongly cannot be corrected by asking the node to change it.
    static bool interval_in_range(uint32_t seconds)
    {
        return seconds >= kIntervalMinSeconds && seconds <= kIntervalMaxSeconds;
    }

    // Validates, stores, and persists. Returns false for an out-of-range value, which is
    // then ignored entirely rather than clamped: a downlink asking for something
    // impossible is more likely a corrupted message than a real instruction.
    //
    // Also returns false when the write did not land, and in that case the in-RAM value is
    // rolled back to what is on flash. True therefore means one thing only — the value is
    // stored — which is what lets a caller retry safely. Keeping a failed value live made a
    // retry a silent no-op: the second call matched the unwritten value, returned true, and
    // the caller dropped the command believing it had persisted. Applying a value that is not
    // yet stored is the caller's job (see main.cpp's pending_interval), because only the
    // caller knows how long it intends to keep trying.
    bool set_interval_seconds(uint32_t seconds);

    uint32_t boot_count() const { return m_boots; }

    // Writes the deferred boot count, if one is pending. Returns false when nothing was due or
    // the write failed.
    //
    // Split from begin() because begin() runs before power::Brownout exists, so a save() there
    // is a flash erase and rewrite at whatever voltage the pack happens to be at — and the boot
    // counter is due precisely when the node is resetting repeatedly, which is when the supply
    // is least trustworthy. The count still increments in RAM at boot; only the write moves,
    // to a point in the cycle where the gate can refuse it. A count that under-reports by a few
    // boots costs nothing: what it is for is noticing that the number climbs at all.
    //
    // Nothing is cleared until an attempt is actually made, so a boot spent entirely under a
    // hold writes on the first later boot where the gate permits it.
    //
    // Called from setup(), directly after the gate is restored and before anything that can hang.
    // It was briefly called from the main cycle instead, which put it after both sensor reads —
    // so a node resetting inside a hung read never reached it, and the counter stopped moving in
    // the one failure it is there to report.
    bool persist_boot_count_if_due();

    // Whether the brownout gate was engaged when this node last wrote its settings.
    //
    // Persisted because a gate living only in RAM fails open: every reset returns the node to
    // transmit-allowed, and a pack that stopped answering because it was low leaves no evidence
    // to correct that. See power::Brownout.
    bool brownout_engaged() const { return m_brownout_engaged; }

    // Records a change in the gate. No write when the value is unchanged — flash cycles are
    // a consumable, and this is called on every state change rather than only on the rare
    // ones. Returns false if the write failed, which means the hold is active now but will
    // not survive a reset.
    bool set_brownout_engaged(bool engaged);

    // Rewrites every in-RAM value after session recovery formats the shared filesystem.
    // Deliberately narrower than making save() public: ordinary callers must use the setters so
    // range checks, rollback, and write coalescing cannot be bypassed.
    bool rewrite_after_filesystem_format();

  private:
    bool load();
    bool save();

    uint32_t m_interval         = kIntervalDefaultSeconds;
    uint32_t m_boots            = 0;
    bool     m_mounted          = false;
    bool     m_brownout_engaged = false;

    // Set at boot when the count is due to be written, cleared by the deferred write above.
    bool     m_boot_write_pending = false;
};

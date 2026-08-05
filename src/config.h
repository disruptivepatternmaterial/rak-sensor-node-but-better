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
// than anything the hardware cares about.
//
// 1800 s, not the 300 s originally specified. The network's fair-use allowance is about 30
// seconds of transmit time per device per day, and one 11-byte uplink at the slowest US915
// data rate takes roughly 370 ms of that. At 300 s the node would transmit 288 times a day
// — around 107 seconds of airtime, more than three times the allowance. At 1800 s it is
// under 18 seconds even at the slowest rate, and comfortably lower at every faster one.
//
// The slow rate is not a corner case. Adaptive data rate moves nodes with weak coverage
// down, so the node most likely to sit at the worst rate is the remote one this design is
// for. A guard that only holds in good coverage would be no guard at all.
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
constexpr uint32_t kIntervalMinSeconds     = 1800;
constexpr uint32_t kIntervalMaxSeconds     = 86400;
constexpr uint32_t kIntervalDefaultSeconds = 3600;
#endif

class Config {
  public:
    // Mounts the filesystem and loads stored values. Any failure leaves the defaults in
    // place and reports it — an unreadable filesystem must not stop the node reporting.
    void begin();

    uint32_t interval_seconds() const { return m_interval; }

    // Validates, stores, and persists. Returns false for an out-of-range value, which is
    // then ignored entirely rather than clamped: a downlink asking for something
    // impossible is more likely a corrupted message than a real instruction.
    bool set_interval_seconds(uint32_t seconds);

    uint32_t boot_count() const { return m_boots; }

  private:
    bool load();
    bool save();

    uint32_t m_interval = kIntervalDefaultSeconds;
    uint32_t m_boots    = 0;
    bool     m_mounted  = false;
};

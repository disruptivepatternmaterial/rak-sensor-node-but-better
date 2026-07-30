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
constexpr uint32_t kIntervalMinSeconds     = 1800;
constexpr uint32_t kIntervalMaxSeconds     = 86400;
constexpr uint32_t kIntervalDefaultSeconds = 3600;

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

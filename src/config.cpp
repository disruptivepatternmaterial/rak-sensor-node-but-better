#include "config.h"

#include "build_features.h"

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

using namespace Adafruit_LittleFS_Namespace;

namespace {

constexpr char kPath[] = "/config.bin";

// A magic number and a version, so a file written by different firmware is recognized as
// foreign and ignored rather than read as garbage settings.
constexpr uint32_t kMagic   = 0x524B534E; // "RKSN"
constexpr uint16_t kVersion = 1;

// Boots between writes of the boot counter. Writing every boot is fine while the node is
// healthy — a handful of writes a year — but the counter matters most in exactly the
// situation where it is dangerous: a node stuck resetting is booting continuously, and
// writing flash on each pass adds wear and a corruption window to a device already in
// trouble. Persisting periodically caps that. The cost is that the count can under-report
// by up to this many after a reset, which does not affect what it is for: noticing that
// the number climbs at all.
constexpr uint32_t kBootPersistEvery = 8;

struct Stored {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t interval;
    uint32_t boots;
};

} // namespace

void Config::begin()
{
    m_mounted = InternalFS.begin();
    if (!m_mounted) {
        LOGLN(F("   config  : filesystem unavailable — running on defaults"));
        return;
    }

#if FEATURE_BENCH_INTERVAL
    // The stored interval is deliberately not read on a bench build, and the bench value is
    // never written back. A field image leaves 900-86400 s in flash, all of which is inside
    // the bench build's widened range, so loading it would quietly restore a half-hour
    // cadence on a board the operator is standing in front of — the setting would look
    // ignored. Not writing 60 s back keeps the flash clean for the next field image, which
    // would reject it as out of range anyway.
    LOGF("   config  : bench build — interval forced to %lu s, stored value ignored\n",
         (unsigned long)kIntervalDefaultSeconds);
    m_interval = kIntervalDefaultSeconds;
#else
    if (!load()) {
        LOGLN(F("   config  : no stored settings — writing defaults"));
    }
#endif

    // The boot counter is the cheapest possible watchdog diagnostic: if it climbs between
    // uplinks, something is resetting the node.
    m_boots++;
#if !FEATURE_BENCH_INTERVAL
    if ((m_boots % kBootPersistEvery) == 0) {
        save();
    }
#endif

    LOGF("   config  : interval %lu s, boot #%lu\n", (unsigned long)m_interval,
         (unsigned long)m_boots);
}

bool Config::load()
{
    File f(InternalFS);
    if (!f.open(kPath, FILE_O_READ)) {
        return false;
    }

    Stored s = {};
    const int n = f.read((uint8_t *)&s, sizeof(s));
    f.close();

    if (n != (int)sizeof(s) || s.magic != kMagic || s.version != kVersion) {
        return false;
    }

    // Range-check on the way in as well as on the way out. Flash can degrade, and a
    // corrupted interval is the difference between reporting hourly and transmitting
    // continuously until the pack is flat.
    if (s.interval < kIntervalMinSeconds || s.interval > kIntervalMaxSeconds) {
        LOGLN(F("   config  : stored interval out of range — using default"));
        return false;
    }

    m_interval = s.interval;
    m_boots    = s.boots;
    return true;
}

bool Config::save()
{
    if (!m_mounted) {
        return false;
    }

    Stored s = {};
    s.magic    = kMagic;
    s.version  = kVersion;
    s.interval = m_interval;
    s.boots    = m_boots;

    InternalFS.remove(kPath);

    File f(InternalFS);
    if (!f.open(kPath, FILE_O_WRITE)) {
        LOGLN(F("   config  : write failed"));
        return false;
    }

    const size_t written = f.write((const uint8_t *)&s, sizeof(s));
    f.close();
    return written == sizeof(s);
}

bool Config::set_interval_seconds(uint32_t seconds)
{
    if (seconds < kIntervalMinSeconds || seconds > kIntervalMaxSeconds) {
        LOGF("   config  : rejected interval %lu s (allowed %lu-%lu)\n",
             (unsigned long)seconds, (unsigned long)kIntervalMinSeconds,
             (unsigned long)kIntervalMaxSeconds);
        return false;
    }

    if (seconds == m_interval) {
        return true; // no write — flash cycles are a consumable
    }

    m_interval = seconds;

    // A failed write is worth reporting rather than swallowing: the node will honor the
    // new interval until the next reset and then silently revert to the old one, which
    // from a distance looks like the downlink was ignored days later for no reason.
    if (!save()) {
        LOGF("   config  : interval %lu s active but NOT saved — reverts on reset\n",
             (unsigned long)m_interval);
        return false;
    }

    LOGF("   config  : interval now %lu s\n", (unsigned long)m_interval);
    return true;
}

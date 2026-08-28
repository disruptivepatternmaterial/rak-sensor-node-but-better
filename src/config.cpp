#include "config.h"

#include "build_features.h"
#include "storage.h"

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

#include <stddef.h>

using namespace Adafruit_LittleFS_Namespace;

namespace {

constexpr char kPath[] = "/config.bin";

// Staging path for save(). A crash between the write and the rename leaves this behind and
// kPath untouched, which is the point; the next save truncates it.
constexpr char kTmpPath[] = "/config.tmp";

// A magic number and a version, so a file written by different firmware is recognized as
// foreign and ignored rather than read as garbage settings.
constexpr uint32_t kMagic   = 0x524B534E; // "RKSN"

// Version 2 appended the persisted brownout bit. Version 1 records are still read rather
// than discarded: rejecting them would throw away a stored interval, and an operator who
// had set a cadence by downlink would find the node quietly back on the default after the
// next firmware update, with nothing to explain it.
constexpr uint16_t kVersion       = 2;
constexpr uint16_t kVersionLegacy = 1;

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
    uint8_t  brownout_engaged; // v2 and later
    uint8_t  pad[3];
};

// The v1 layout, kept so a file written by earlier firmware can still be read. Everything
// up to and including `boots` is byte-identical to the struct above.
constexpr size_t kStoredSizeV1 = offsetof(Stored, brownout_engaged);

} // namespace

void Config::begin()
{
    // InternalFileSystem::begin() automatically erases and formats after one failed mount.
    // This runs before the first pack reading and before Brownout's flash gate exists, so that
    // fallback can erase Config and the saved session on an unknown or sagging supply. Mount
    // non-destructively here; session recovery may format later only after the gate says it is
    // safe.
    //
    // CITE(prior-art): [CIT-INTERNAL-FS] derived begin() erases all seven filesystem pages on
    //   mount failure before retrying.
    // CITE(spec): docs/FIRMWARE_SPEC.md §7 H3 — no flash erase/write before voltage evidence.
    m_mounted = storage::mount_without_format();
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
    // Marked, not written. This runs before power::Brownout is constructed from the persisted
    // bit, so writing here erases and rewrites a flash page with no gate in front of it — and
    // the count comes due exactly when the node is resetting in a loop, which is the state where
    // the pack is least able to carry the write and most able to be interrupted mid-way. The
    // write is handed to the main cycle instead, after the first battery reading has told the
    // gate whether flash is affordable. See Config::persist_boot_count_if_due().
    //
    // CITE(spec): docs/FIRMWARE_SPEC.md §7 H3 — "Brownout: no flash thrash". This path was
    //   outside that requirement purely because of where it sat in the boot order.
    // CITE(datasheet): [CIT-NRF-POWER] the internal flash behind this write; an erase-and-write
    //   is the largest non-radio current the node draws and the only one that can leave state
    //   behind it.
    // CITE(prior-art): [CIT-LITTLEFS-DESIGN] the filesystem commits atomically, which bounds the
    //   damage of an interrupted write to a lost update rather than a corrupted record — the
    //   reason this is a power and thrash defect rather than a corruption one.
    m_boot_write_pending = ((m_boots % kBootPersistEvery) == 0);
#endif

    LOGF("   config  : interval %lu s, boot #%lu\n", (unsigned long)m_interval,
         (unsigned long)m_boots);
}

bool Config::persist_boot_count_if_due()
{
    if (!m_boot_write_pending) {
        return false;
    }

    // Cleared on the attempt, not on success. A failed write leaves the count behind by up to
    // kBootPersistEvery, which is what the counter already tolerates by design; retrying it every
    // cycle would turn a full or failing filesystem into a write on every wake, which is the
    // thrash H3 forbids.
    m_boot_write_pending = false;
    return save();
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

    if (n < (int)kStoredSizeV1 || s.magic != kMagic) {
        return false;
    }

    if (s.version == kVersionLegacy) {
        // Written before the brownout bit existed. The interval and boot count are in the
        // same place, so they are honored; the gate simply starts disengaged, which is the
        // behavior that firmware had. It is not a hole — a genuinely low pack re-engages
        // the gate on the first valid reading, and a silent one re-engages on the
        // consecutive-invalid-read path.
        s.brownout_engaged = 0;
    } else if (s.version != kVersion || n != (int)sizeof(s)) {
        return false;
    }

    // Range-check on the way in as well as on the way out. Flash can degrade, and a
    // corrupted interval is the difference between reporting hourly and transmitting
    // continuously until the pack is flat.
    if (s.interval < kIntervalMinSeconds || s.interval > kIntervalMaxSeconds) {
        LOGLN(F("   config  : stored interval out of range — using default"));
        return false;
    }

    m_interval         = s.interval;
    m_boots            = s.boots;
    m_brownout_engaged = (s.brownout_engaged != 0);
    return true;
}

bool Config::save()
{
    if (!m_mounted) {
        return false;
    }

    Stored s = {};
    s.magic            = kMagic;
    s.version          = kVersion;
    s.interval         = m_interval;
    s.boots            = m_boots;
    s.brownout_engaged = m_brownout_engaged ? 1 : 0;

    // Staged and renamed, never written over the live record. The old shape removed kPath and
    // then opened it, so a write that failed for any reason destroyed the stored config first --
    // and this record is worse to lose than the session one. It carries m_brownout_engaged,
    // which is how a brownout hold survives a reset; losing it means the node comes back with no
    // memory of having decided to stop transmitting, on a pack that may still be flat. That is
    // the fail-open hole #38 closed, reopened by a failed write. It also carries the interval, so
    // a downlink-set 900 s would silently revert to the compiled default.
    //
    // Cost is one extra littlefs block while the write is in flight; Stored is a few dozen bytes
    // against a 28 KB partition shared only with /session.bin.
    //
    // CITE(prior-art): [CIT-LITTLEFS-DESIGN] remove and rename are atomic even on power loss, so
    //   kPath is always either the previous config or the new one
    // CITE(spec): docs/FIRMWARE_SPEC.md §7 H5 -- "Interval + keys path survives power loss"; a
    //   failed write that erases the interval does not survive it
    if (!storage::atomic_write(kPath, kTmpPath, &s, sizeof(s))) {
        LOGLN(F("   config  : write failed — old record preserved"));
        return false;
    }

    // Every save writes m_boots along with everything else, so a save taken for any other
    // reason has already discharged whatever the boot counter owed. Without clearing this,
    // set_interval_seconds() or set_brownout_engaged() followed by the deferred boot write
    // erased and rewrote the same page twice in one run — in a change whose whole purpose was
    // to write it less often.
    //
    // CITE(spec): docs/FIRMWARE_SPEC.md §7 H3 — "no flash thrash"; two writes of one page for
    //   one record is the thrash, whatever the two callers thought they were doing.
    m_boot_write_pending = false;
    return true;
}

bool Config::rewrite_after_filesystem_format()
{
    // storage::format_and_mount() succeeded immediately before this callback. Config may still
    // remember a failed non-destructive boot mount, so synchronize its view before save().
    m_mounted = true;
    return save();
}

bool Config::set_interval_seconds(uint32_t seconds)
{
    if (!interval_in_range(seconds)) {
        LOGF("   config  : rejected interval %lu s (allowed %lu-%lu)\n",
             (unsigned long)seconds, (unsigned long)kIntervalMinSeconds,
             (unsigned long)kIntervalMaxSeconds);
        return false;
    }

    if (seconds == m_interval) {
        return true; // no write — flash cycles are a consumable
    }

    // Staged, and rolled back if the write does not land. Leaving the new value in m_interval
    // after a failed save() made the *second* attempt a silent no-op: the `seconds ==
    // m_interval` test above returned true without writing anything, so a caller retrying a
    // deferred set-interval cleared its pending value believing it had persisted. The command
    // was then dropped, having already told the console it would survive the reset. Reachable
    // on every retry path and on every save() while the filesystem is unmounted, where save()
    // returns false without ever touching flash.
    //
    // Rolling back rather than keeping it live is what makes the retry honest: the value stays
    // applied in RAM by the caller that owns the retry (main.cpp), and this object reports
    // only what is actually on flash. Refs #65.
    //
    // CITE(spec): docs/FIRMWARE_SPEC.md §7 H5 — the interval must survive power loss, which is
    //   a claim this function is the only thing entitled to make.
    // CITE(spec): [CIT-LW-LINK] Class A — a downlink can only follow an uplink, so a dropped
    //   set-interval cannot be re-sent on demand; the network has already drained its queue.
    // CITE(prior-art): [CIT-LITTLEFS-DESIGN] "All POSIX operations ... are atomic, even in
    //   event of power-loss" — a failed write leaves the previous record intact, so the stored
    //   value really is the old one and rolling RAM back to match it is not a guess.
    const uint32_t previous = m_interval;
    m_interval              = seconds;

    if (!save()) {
        m_interval = previous;
        LOGF("   config  : interval %lu s NOT saved — still %lu s on flash\n",
             (unsigned long)seconds, (unsigned long)m_interval);
        return false;
    }

    LOGF("   config  : interval now %lu s\n", (unsigned long)m_interval);
    return true;
}

bool Config::set_brownout_engaged(bool engaged)
{
    if (engaged == m_brownout_engaged) {
        return true; // no write — flash cycles are a consumable
    }

    m_brownout_engaged = engaged;

#if FEATURE_BENCH_INTERVAL
    // A bench build never writes settings back, for the same reason it never reads them:
    // its 60 s interval is out of range for a field image and would be discarded, and the
    // whole record would be rewritten to carry one bit. The gate still works in RAM for the
    // run, which is all a bench build needs.
    return true;
#else
    if (!save()) {
        LOGLN(F("   config  : brownout state active but NOT saved — clears on reset"));
        return false;
    }
    return true;
#endif
}

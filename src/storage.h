#pragma once

#include <stddef.h>

namespace storage {

enum class ReadResult {
    Ok,
    Absent,
    WrongSize,
    InvalidRecord,
    IoError,
};

// Replaces path atomically with exactly size bytes from data. Every operation that can delay
// an error until commit -- sync and close included -- must succeed before the staging file is
// promoted.
bool atomic_write(const char *path, const char *staging_path, const void *data, size_t size);

// Reads exactly size bytes and preserves the distinction between a missing file, a malformed
// record, and an I/O error. The Adafruit File wrapper collapses those at open.
ReadResult read_exact(const char *path, void *data, size_t size);

// True only when the path is now absent. Unlike Adafruit_LittleFS::remove(), this treats
// LFS_ERR_NOENT as success while preserving every real I/O error as failure.
bool remove_or_absent(const char *path);

// Mounts without InternalFileSystem::begin()'s automatic erase-and-format fallback. Boot calls
// this before voltage is known, so a failed mount must preserve flash for later gated repair.
bool mount_without_format();

// Formats and then explicitly mounts. Adafruit format() can leave the filesystem unmounted on
// failure, and skips remount on a later call that began unmounted.
bool format_and_mount();

} // namespace storage

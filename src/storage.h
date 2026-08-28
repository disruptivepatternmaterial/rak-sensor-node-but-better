#pragma once

#include <stddef.h>

namespace storage {

// Replaces path atomically with exactly size bytes from data. Every operation that can delay
// an error until commit -- sync and close included -- must succeed before the staging file is
// promoted.
bool atomic_write(const char *path, const char *staging_path, const void *data, size_t size);

// True only when the path is now absent. Unlike Adafruit_LittleFS::remove(), this treats
// LFS_ERR_NOENT as success while preserving every real I/O error as failure.
bool remove_or_absent(const char *path);

} // namespace storage

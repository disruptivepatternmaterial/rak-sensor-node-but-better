#include "storage.h"

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

namespace storage {

bool atomic_write(const char *path, const char *staging_path, const void *data, size_t size)
{
    // The Adafruit File wrapper is intentionally not used here. Its write() reports immediate
    // write errors, but flush() and close() return void and discard the lfs_file_sync() /
    // lfs_file_close() status. littlefs commits updates at sync or close, so promoting the
    // staging file after an ignored close error can replace a valid record with stale data.
    //
    // The low-level API is public through _getFS()/_lockFS() for internal use. Holding the one
    // filesystem mutex across the whole transaction also prevents another task from observing
    // the staging path between write and rename.
    //
    // CITE(prior-art): [CIT-ADA-LITTLEFS] Adafruit_LittleFS_File.cpp — File::flush() and
    //   File::_close() discard lfs_file_sync()/lfs_file_close() errors; _getFS() and the lock
    //   methods are public specifically for internal use.
    // CITE(prior-art): [CIT-LITTLEFS-DESIGN] file updates commit on sync/close and rename is
    //   atomic even across power loss.
    lfs_t *fs = InternalFS._getFS();
    InternalFS._lockFS();

    lfs_file_t file = {};
    int        status =
        lfs_file_open(fs, &file, staging_path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    bool opened = (status == LFS_ERR_OK);

    if (opened) {
        const lfs_ssize_t written = lfs_file_write(fs, &file, data, size);
        if (written != (lfs_ssize_t)size) {
            status = (written < 0) ? (int)written : LFS_ERR_IO;
        }

        if (status == LFS_ERR_OK) {
            status = lfs_file_sync(fs, &file);
        }

        const int close_status = lfs_file_close(fs, &file);
        if (status == LFS_ERR_OK) {
            status = close_status;
        }
    }

    if (status == LFS_ERR_OK) {
        status = lfs_rename(fs, staging_path, path);
    }

    if (status != LFS_ERR_OK) {
        // Cleanup is best effort. A surviving staging file is harmless: LFS_O_TRUNC replaces
        // its contents on the next attempt, and the live path was never touched.
        (void)lfs_remove(fs, staging_path);
    }

    InternalFS._unlockFS();
    return status == LFS_ERR_OK;
}

ReadResult read_exact(const char *path, void *data, size_t size)
{
    lfs_t *fs = InternalFS._getFS();
    InternalFS._lockFS();

    lfs_file_t file = {};
    int        status = lfs_file_open(fs, &file, path, LFS_O_RDONLY);
    if (status == LFS_ERR_NOENT) {
        InternalFS._unlockFS();
        return ReadResult::Absent;
    }
    if (status != LFS_ERR_OK) {
        InternalFS._unlockFS();
        return ReadResult::IoError;
    }

    ReadResult result = ReadResult::Ok;
    const lfs_soff_t stored_size = lfs_file_size(fs, &file);
    if (stored_size < 0) {
        result = ReadResult::IoError;
    } else if ((size_t)stored_size != size) {
        result = ReadResult::WrongSize;
    } else {
        const lfs_ssize_t read = lfs_file_read(fs, &file, data, size);
        if (read < 0) {
            result = ReadResult::IoError;
        } else if ((size_t)read != size) {
            result = ReadResult::WrongSize;
        }
    }

    if (lfs_file_close(fs, &file) != LFS_ERR_OK) {
        result = ReadResult::IoError;
    }

    InternalFS._unlockFS();
    return result;
}

bool remove_or_absent(const char *path)
{
    // Adafruit_LittleFS::remove() returns false for both LFS_ERR_NOENT and real I/O errors.
    // Adafruit_LittleFS::exists() is no better for disambiguation: it returns false for every
    // lfs_stat error. The raw status is the only truthful answer, and this distinction is
    // load-bearing for session replay protection.
    //
    // CITE(prior-art): [CIT-ADA-LITTLEFS] Adafruit_LittleFS.cpp — remove() is true only for
    //   LFS_ERR_OK, while exists() collapses all lfs_stat errors to false.
    lfs_t *fs = InternalFS._getFS();
    InternalFS._lockFS();
    const int status = lfs_remove(fs, path);
    InternalFS._unlockFS();

    return status == LFS_ERR_OK || status == LFS_ERR_NOENT;
}

bool mount_without_format()
{
    // InternalFileSystem::begin() is not a mount operation: on mount failure it erases all seven
    // flash pages, formats, and retries. At boot that runs before Brownout has any voltage
    // evidence, so selecting the base implementation is the only non-destructive mount.
    //
    // CITE(prior-art): [CIT-INTERNAL-FS] InternalFileSystem.cpp — derived begin() erases every
    //   filesystem page after one failed Adafruit_LittleFS::begin().
    return static_cast<Adafruit_LittleFS &>(InternalFS).begin();
}

bool format_and_mount()
{
    // format() records whether it was mounted on entry and remounts only in that case. If an
    // unmount, format, or remount step fails, its internal mounted bit may be false; the next
    // successful format then sees "started unmounted" and deliberately skips remount. Always
    // call begin() afterward so both the first failed attempt and a later successful retry end
    // in a truthful mounted state.
    //
    // CITE(prior-art): [CIT-ADA-LITTLEFS] Adafruit_LittleFS::format() captures
    //   `attemptMount = _mounted`, clears `_mounted` before unmount, and remounts only when
    //   attemptMount was true.
    const bool formatted = InternalFS.format();
    const bool mounted   = mount_without_format();
    return formatted && mounted;
}

} // namespace storage

#pragma once

#include <filesystem>
#include <string>
#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace platform::linux_runtime
{
// A normal open/close of a SQLite inode drops ALL POSIX locks held by this
// process on that inode. O_PATH handles do not participate in those locks.
// SQLite must create/open the file before this ownership/permission check.
inline bool protect_private_database_file(const std::filesystem::path& path)
{
#if defined(__linux__) && defined(O_PATH)
    const int descriptor = ::open(path.c_str(), O_PATH | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) return false;
    struct stat status
    {
    };
    const bool regular = ::fstat(descriptor, &status) == 0 && S_ISREG(status.st_mode) && status.st_uid == ::geteuid();
    // chmod resolves the pinned O_PATH inode through procfs without opening a
    // second read/write descriptor. A substituted path cannot redirect it.
    const std::string pinned = "/proc/self/fd/" + std::to_string(descriptor);
    const bool protected_file = regular && ::chmod(pinned.c_str(), 0600) == 0;
    ::close(descriptor);
    return protected_file;
#else
    (void)path;
    return false;
#endif
}
} // namespace platform::linux_runtime

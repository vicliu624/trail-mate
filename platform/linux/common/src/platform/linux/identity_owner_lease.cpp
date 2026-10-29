#include "platform/linux/identity_owner_lease.h"
#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace platform::linux_runtime
{
bool set_identity_owner_lock(int descriptor, bool exclusive)
{
#if defined(__linux__) && defined(F_OFD_SETLK)
    struct flock lock
    {
    };
    lock.l_type = exclusive ? F_WRLCK : F_RDLCK;
    lock.l_whence = SEEK_SET;
    return descriptor >= 0 && ::fcntl(descriptor, F_OFD_SETLK, &lock) == 0;
#else
    (void)descriptor;
    (void)exclusive;
    return false;
#endif
}
bool open_shared_identity_owner(const std::filesystem::path& path, int& descriptor)
{
    if (descriptor >= 0) return true;
#if defined(__linux__)
    if (path.empty()) return false;
    std::error_code error;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    const int opened = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (opened < 0) return false;
    struct stat status
    {
    };
    if (::fstat(opened, &status) != 0 || !S_ISREG(status.st_mode) || status.st_uid != ::geteuid() ||
        ::fchmod(opened, 0600) != 0 || !set_identity_owner_lock(opened, false))
    {
        ::close(opened);
        return false;
    }
    descriptor = opened;
    return true;
#else
    (void)path;
    return false;
#endif
}
std::filesystem::path radio_identity_owner_path(const std::filesystem::path& database)
{
    return database.string() + ".radio-identity-owner";
}
IdentityOwnerLease::~IdentityOwnerLease() { reset(); }
bool IdentityOwnerLease::acquireShared(const std::filesystem::path& path)
{
    const auto normalized = path.lexically_normal();
    if (descriptor_ >= 0) return path_ == normalized;
    if (!open_shared_identity_owner(normalized, descriptor_)) return false;
    path_ = normalized;
    return true;
}
bool IdentityOwnerLease::acquireExclusive() { return set_identity_owner_lock(descriptor_, true); }
bool IdentityOwnerLease::downgrade() { return set_identity_owner_lock(descriptor_, false); }
void IdentityOwnerLease::reset()
{
#if defined(__linux__)
    if (descriptor_ >= 0) ::close(descriptor_);
#endif
    descriptor_ = -1;
    path_.clear();
}
} // namespace platform::linux_runtime

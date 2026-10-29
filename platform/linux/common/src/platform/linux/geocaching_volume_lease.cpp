#include "platform/linux/geocaching_volume_lease.h"
#include <algorithm>
#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace platform::linux_runtime
{
namespace gc = ::geocaching;
namespace
{
using Result = LinuxGeocachingVolumeLease::Result;
bool nonzero(const LinuxGeocachingVolumeLease::Volume& value)
{
    return std::any_of(value.begin(), value.end(), [](uint8_t byte)
                       { return byte != 0; });
}
#if defined(__linux__)
struct Fd
{
    int value = -1;
    explicit Fd(int fd = -1) : value(fd) {}
    ~Fd()
    {
        if (value >= 0) ::close(value);
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    int take()
    {
        const auto fd = value;
        value = -1;
        return fd;
    }
};
Result error() { return errno == ELOOP || errno == ENOTDIR ? Result::Unsafe : errno == ENOENT ? Result::Missing
                                                                                              : Result::IoError; }
bool identity(const struct stat& a, const struct stat& b) { return a.st_dev == b.st_dev && a.st_ino == b.st_ino; }
bool stamp(const struct stat& a, const struct stat& b)
{
    return identity(a, b) && a.st_size == b.st_size && a.st_nlink == b.st_nlink &&
           a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec &&
           a.st_ctim.tv_sec == b.st_ctim.tv_sec && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}
Result header(int state, LinuxGeocachingVolumeLease::Volume& out, struct stat& info)
{
    out = {};
    Fd file(::openat(state, "format.bin", O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    if (file.value < 0) return errno == ENOENT ? Result::Corrupt : error();
    if (::fstat(file.value, &info)) return Result::IoError;
    if (!S_ISREG(info.st_mode) || info.st_nlink != 1) return Result::Unsafe;
    if (info.st_size < 8) return Result::Corrupt;
    gc::storage::VolumeHeader bytes{};
    const auto size = std::min<uint64_t>(static_cast<uint64_t>(info.st_size), bytes.size());
    size_t offset = 0;
    while (offset < size)
    {
        const auto count = ::read(file.value, bytes.data() + offset, size - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return Result::IoError;
        offset += static_cast<size_t>(count);
    }
    const auto decoded = gc::storage::decodeVolumeHeader({bytes.data(), offset}, out);
    if (decoded == gc::storage::VolumeFormatResult::Unsupported) return Result::Unsupported;
    if (decoded != gc::storage::VolumeFormatResult::Supported || info.st_size != static_cast<off_t>(bytes.size()) || !nonzero(out)) return Result::Corrupt;
    struct stat after
    {
    };
    if (::fstat(file.value, &after)) return Result::IoError;
    return stamp(info, after) ? Result::Ready : Result::Stale;
}
#endif
} // namespace
struct LinuxGeocachingVolumeLease::Impl
{
    explicit Impl(std::filesystem::path path) : root(std::move(path)) {}
    ~Impl() { release(); }
    void release()
    {
#if defined(__linux__)
        if (lock >= 0) ::close(lock);
        if (state >= 0) ::close(state);
        if (base >= 0) ::close(base);
        lock = state = base = -1;
#endif
        volume = {};
    }
    std::filesystem::path root;
    Volume volume{};
#if defined(__linux__)
    int base = -1, state = -1, lock = -1;
    struct stat base_info
    {
    }, state_info{}, lock_info{}, format_info{};
#endif
};
LinuxGeocachingVolumeLease::LinuxGeocachingVolumeLease(std::filesystem::path root) : impl_(std::make_unique<Impl>(std::move(root))) {}
LinuxGeocachingVolumeLease::~LinuxGeocachingVolumeLease() = default;
void LinuxGeocachingVolumeLease::release() { impl_->release(); }
Result LinuxGeocachingVolumeLease::initialize(const Volume& volume)
{
    if (!nonzero(volume)) return Result::Invalid;
#if defined(__linux__)
    if (impl_->lock >= 0) return Result::Busy;
    Fd base(::open(impl_->root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (base.value < 0) return errno == ENOENT ? Result::Unavailable : error();
    if (::mkdirat(base.value, ".state", 0700)) return errno == EEXIST ? Result::Exists : Result::IoError;
    if (::fsync(base.value)) return Result::IoError;
    Fd state(::openat(base.value, ".state", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (state.value < 0) return error();
    Fd file(::openat(state.value, "format.bin", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (file.value < 0) return error();
    const auto bytes = gc::storage::encodeVolumeHeader(volume);
    size_t offset = 0;
    while (offset < bytes.size())
    {
        const auto count = ::write(file.value, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return Result::IoError;
        offset += static_cast<size_t>(count);
    }
    if (::fsync(file.value) || ::fsync(state.value)) return Result::IoError;
    const int fd = file.take();
    if (::close(fd)) return Result::IoError;
    Volume confirmed{};
    struct stat info
    {
    };
    const auto verified = header(state.value, confirmed, info);
    return verified == Result::Ready && confirmed != volume ? Result::Mismatch : verified;
#else
    return Result::Unavailable;
#endif
}
Result LinuxGeocachingVolumeLease::acquire(Volume& observed)
{
    observed = {};
#if defined(__linux__)
    if (impl_->lock >= 0)
    {
        const auto result = check(impl_->volume);
        if (result == Result::Ready) observed = impl_->volume;
        return result;
    }
    Fd base(::open(impl_->root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (base.value < 0) return errno == ENOENT ? Result::Unavailable : error();
    Fd state(::openat(base.value, ".state", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (state.value < 0) return error();
    struct stat base_info
    {
    }, state_info{}, format_info{}, lock_info{};
    if (::fstat(base.value, &base_info) || ::fstat(state.value, &state_info)) return Result::IoError;
    Volume volume{};
    const auto verified = header(state.value, volume, format_info);
    if (verified != Result::Ready) return verified;
    if (base_info.st_dev != state_info.st_dev || state_info.st_dev != format_info.st_dev) return Result::Unsafe;
    Fd lock(::openat(state.value, "owner.lock", O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600));
    if (lock.value < 0) return error();
    if (::fstat(lock.value, &lock_info)) return Result::IoError;
    if (!S_ISREG(lock_info.st_mode) || lock_info.st_nlink != 1 || lock_info.st_dev != state_info.st_dev) return Result::Unsafe;
    int locked;
    do
    {
        locked = ::flock(lock.value, LOCK_EX | LOCK_NB);
    } while (locked && errno == EINTR);
    if (locked) return errno == EWOULDBLOCK || errno == EAGAIN ? Result::Busy : Result::IoError;
    if (::fsync(lock.value) || ::fsync(state.value)) return Result::IoError;
    impl_->base = base.take();
    impl_->state = state.take();
    impl_->lock = lock.take();
    impl_->base_info = base_info;
    impl_->state_info = state_info;
    impl_->format_info = format_info;
    impl_->lock_info = lock_info;
    impl_->volume = volume;
    const auto checked = check(volume);
    if (checked != Result::Ready)
    {
        impl_->release();
        return checked;
    }
    observed = volume;
    return Result::Ready;
#else
    return Result::Unavailable;
#endif
}
Result LinuxGeocachingVolumeLease::check(const Volume& expected) const
{
#if defined(__linux__)
    if (impl_->lock < 0) return Result::Unavailable;
    if (expected != impl_->volume) return Result::Mismatch;
    Fd base(::open(impl_->root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (base.value < 0) return error();
    struct stat base_info
    {
    }, state_info{}, format_info{}, lock_info{}, held_lock{};
    if (::fstat(base.value, &base_info)) return Result::IoError;
    if (!identity(base_info, impl_->base_info)) return Result::Stale;
    Fd state(::openat(base.value, ".state", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (state.value < 0) return error();
    if (::fstat(state.value, &state_info)) return Result::IoError;
    if (!identity(state_info, impl_->state_info)) return Result::Stale;
    Volume observed{};
    const auto verified = header(state.value, observed, format_info);
    if (verified != Result::Ready) return verified;
    if (observed != expected) return Result::Mismatch;
    if (!stamp(format_info, impl_->format_info)) return Result::Stale;
    if (::fstatat(state.value, "owner.lock", &lock_info, AT_SYMLINK_NOFOLLOW) || ::fstat(impl_->lock, &held_lock)) return error();
    if (!S_ISREG(lock_info.st_mode) || lock_info.st_nlink != 1 || held_lock.st_nlink != 1 ||
        !identity(lock_info, impl_->lock_info) || !identity(held_lock, impl_->lock_info)) return Result::Stale;
    return Result::Ready;
#else
    (void)expected;
    return Result::Unavailable;
#endif
}
int LinuxGeocachingVolumeLease::rootDescriptor() const
{
#if defined(__linux__)
    return check(impl_->volume) == Result::Ready ? impl_->base : -1;
#else
    return -1;
#endif
}
} // namespace platform::linux_runtime

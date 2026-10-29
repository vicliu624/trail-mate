#pragma once
#include "geocaching/storage/volume_format.h"
#include <filesystem>
#include <memory>

namespace platform::linux_runtime
{
// Exclusive lease for cooperating cache-file writers. Root is injected and must
// already exist. No device discovery, UUID generation or journal reset occurs.
class LinuxGeocachingVolumeLease
{
  public:
    using Volume = ::geocaching::storage::VolumeInstance;
    enum class Result
    {
        Ready,
        Missing,
        Exists,
        Invalid,
        Busy,
        Corrupt,
        Unsupported,
        Mismatch,
        Stale,
        Unsafe,
        Unavailable,
        IoError
    };
    explicit LinuxGeocachingVolumeLease(std::filesystem::path root);
    ~LinuxGeocachingVolumeLease();
    // Only creates a brand-new .state directory. Existing/interrupted state is
    // retained. Caller supplies a fresh nonzero CSPRNG identifier.
    Result initialize(const Volume&);
    Result acquire(Volume& observed);
    Result check(const Volume& expected) const;
    // Borrowed descriptor, never close it. Valid only while the lease is held;
    // callers use openat-style operations and recheck before every mutation.
    int rootDescriptor() const;
    void release();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime

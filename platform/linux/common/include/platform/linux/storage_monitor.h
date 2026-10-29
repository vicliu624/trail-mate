#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>

namespace platform::linux_runtime
{
struct StorageSnapshot
{
    bool available = false;
    bool writable = false;
    bool space_known = false;
    uintmax_t capacity_bytes = 0;
    uintmax_t available_bytes = 0;
};
// Blocking filesystem probe, for worker use. A readable directory can remain
// available for maps/music even when writes are forbidden or space is exhausted.
StorageSnapshot probe_storage_root(const std::filesystem::path& root);
class StorageMonitor
{
  public:
    using Probe = std::function<StorageSnapshot()>;
    explicit StorageMonitor(Probe probe, std::chrono::milliseconds interval = std::chrono::seconds(1));
    ~StorageMonitor();
    StorageSnapshot snapshot() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
StorageSnapshot linux_storage_snapshot();
} // namespace platform::linux_runtime

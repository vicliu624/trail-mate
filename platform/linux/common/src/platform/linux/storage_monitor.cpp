#include "platform/linux/storage_monitor.h"
#include "platform/linux/runtime_paths.h"
#include <algorithm>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>
#if defined(__linux__)
#include <sys/statvfs.h>
#include <unistd.h>
#endif

namespace platform::linux_runtime
{
StorageSnapshot probe_storage_root(const std::filesystem::path& root)
{
    StorageSnapshot out;
    if (root.empty()) return out;
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error || !std::filesystem::is_directory(root, error) || error) return out;
#if defined(__linux__)
    if (::access(root.c_str(), R_OK | X_OK) != 0) return out;
    out.available = true;
    struct statvfs space
    {
    };
    if (::statvfs(root.c_str(), &space) != 0) return out;
    out.writable = !(space.f_flag & ST_RDONLY) && ::access(root.c_str(), W_OK | X_OK) == 0;
    const uintmax_t block = space.f_frsize ? space.f_frsize : space.f_bsize;
    if (!block || space.f_blocks > std::numeric_limits<uintmax_t>::max() / block ||
        space.f_bavail > space.f_blocks) return out;
    out.space_known = true;
    out.capacity_bytes = static_cast<uintmax_t>(space.f_blocks) * block;
    out.available_bytes = static_cast<uintmax_t>(space.f_bavail) * block;
#else
    // Other desktop hosts retain directory availability without compiling Linux
    // syscalls. No writable claim is made without an effective-access probe.
    std::filesystem::directory_iterator readable(root, error);
    if (error) return out;
    out.available = true;
    const auto space = std::filesystem::space(root, error);
    if (!error && space.capacity != std::numeric_limits<uintmax_t>::max() && space.available <= space.capacity)
    {
        out.space_known = true;
        out.capacity_bytes = space.capacity;
        out.available_bytes = space.available;
    }
#endif
    return out;
}

struct StorageMonitor::Impl
{
    Impl(Probe value, std::chrono::milliseconds interval)
        : probe(std::move(value)), period(std::max(interval, std::chrono::milliseconds(10))),
          worker([this]
                 { run(); }) {}
    ~Impl()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        if (worker.joinable()) worker.join();
    }
    void run()
    {
        std::unique_lock<std::mutex> lock(mutex);
        while (!stopping)
        {
            lock.unlock();
            StorageSnapshot next;
            try
            {
                if (probe) next = probe();
            }
            catch (...)
            {
                next = {};
            }
            lock.lock();
            current = next;
            wake.wait_for(lock, period, [this]
                          { return stopping; });
        }
    }
    Probe probe;
    std::chrono::milliseconds period;
    mutable std::mutex mutex;
    std::condition_variable wake;
    StorageSnapshot current;
    bool stopping = false;
    std::thread worker;
};
StorageMonitor::StorageMonitor(Probe probe, std::chrono::milliseconds interval)
    : impl_(std::make_unique<Impl>(std::move(probe), interval)) {}
StorageMonitor::~StorageMonitor() = default;
StorageSnapshot StorageMonitor::snapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->current;
}
StorageSnapshot linux_storage_snapshot()
{
    static StorageMonitor monitor([]
                                  { return probe_storage_root(resolve_paths().sd_root); });
    return monitor.snapshot();
}
} // namespace platform::linux_runtime

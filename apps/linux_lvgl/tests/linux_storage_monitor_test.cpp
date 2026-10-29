#include "platform/linux/storage_monitor.h"
#include "platform/ui/device_runtime.h"
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace
{
using namespace platform::linux_runtime;
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
template <class Predicate>
void await(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (!predicate())
    {
        require(std::chrono::steady_clock::now() < deadline, "storage condition timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
} // namespace
int main(int argc, char** argv)
{
    namespace fs = std::filesystem;
    if (argc == 2)
    {
        (void)linux_storage_snapshot();
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        require(!platform::ui::device::sd_ready() && !platform::ui::device::card_ready(), "file root exposed as ready");
        return 0;
    }
    char pattern[] = "/tmp/trailmate-storage-monitor-XXXXXX";
    require(mkdtemp(pattern), "temporary root failed");
    const fs::path root(pattern), storage = root / "data", file = root / "file";
    std::ofstream(file) << "retain";
    setenv("TRAIL_MATE_SD_ROOT", file.c_str(), 1);
    const auto child = fork();
    require(child >= 0, "fork failed");
    if (child == 0)
    {
        execl(argv[0], argv[0], "--file", nullptr);
        _exit(127);
    }
    int result = 0;
    require(waitpid(child, &result, 0) == child && WIFEXITED(result) && WEXITSTATUS(result) == 0, "file root port regression");
    require(fs::file_size(file) == 6, "probe modified regular file");
    require(!probe_storage_root(file).available, "regular file accepted");
    require(!probe_storage_root(file / "child").available, "non-directory parent accepted");
    require(!probe_storage_root({}).available, "empty root accepted");
    auto value = probe_storage_root(storage);
    require(value.available && value.writable && value.space_known && value.capacity_bytes > 0 &&
                value.available_bytes <= value.capacity_bytes,
            "directory/space probe failed");
    if (geteuid() != 0)
    {
        fs::permissions(storage, fs::perms::owner_read | fs::perms::owner_exec);
        value = probe_storage_root(storage);
        require(value.available && !value.writable, "read-only directory hidden or advertised writable");
        fs::permissions(storage, fs::perms::owner_all);
    }
    {
        StorageMonitor monitor([&]
                               { return probe_storage_root(storage); },
                               std::chrono::milliseconds(10));
        await([&]
              { return monitor.snapshot().available; });
        require(fs::remove(storage), "directory removal failed");
        std::ofstream(storage) << "not a directory";
        await([&]
              { return !monitor.snapshot().available; });
        fs::remove(storage);
        await([&]
              { return monitor.snapshot().available; });
    }
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, released = false;
    {
        StorageMonitor monitor([&]
                               {
            std::unique_lock<std::mutex> lock(mutex);
            entered = true;
            changed.notify_all();
            changed.wait(lock, [&] { return released; });
            return probe_storage_root(storage); });
        {
            std::unique_lock<std::mutex> lock(mutex);
            require(changed.wait_for(lock, std::chrono::seconds(2), [&]
                                     { return entered; }),
                    "probe not entered");
        }
        for (unsigned i = 0; i < 10000; ++i) require(!monitor.snapshot().available, "unpublished probe leaked");
        {
            std::lock_guard<std::mutex> lock(mutex);
            released = true;
        }
        changed.notify_all();
        await([&]
              { return monitor.snapshot().available; });
    }
    setenv("TRAIL_MATE_SD_ROOT", storage.c_str(), 1);
    await([]
          { return platform::ui::device::sd_ready() && platform::ui::device::card_ready(); });
    fs::remove_all(root);
}

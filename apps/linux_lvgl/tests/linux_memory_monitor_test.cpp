#include "platform/linux/memory_monitor.h"
#include <atomic>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace
{
using namespace platform::linux_runtime;
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
auto parse(const std::string& text)
{
    std::istringstream input(text);
    return parse_linux_meminfo(input);
}
template <class Predicate>
void await(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (!predicate())
    {
        require(std::chrono::steady_clock::now() < deadline, "memory monitor condition timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
} // namespace
int main()
{
    auto stats = parse("MemTotal: 4096 kB\nUnknown: 5\nMemAvailable: 1234 kB\n");
    require(stats.ram_total_bytes == 4096 * 1024 && stats.ram_free_bytes == 1234 * 1024,
            "complete sample not parsed");
    require(!stats.psram_available && !stats.psram_total_bytes, "fabricated PSRAM");
    require(parse("MemTotal: 4096 kB\nMemAvailable: 0 kB").ram_total_bytes == 4096 * 1024,
            "zero availability or final line without newline rejected");
    for (const auto& text : {
             "MemTotal: 4096 kB\n", "MemAvailable: 100 kB\n", "MemTotal: 0 kB\nMemAvailable: 0 kB\n",
             "MemTotal: 2 kB\nMemAvailable: 3 kB\n", "MemTotal: -1 kB\nMemAvailable: 0 kB\n",
             "MemTotal: 4096 MB\nMemAvailable: 2 kB\n", "MemTotal: 4096kB\nMemAvailable: 2 kB\n",
             "MemTotal: 4096 kB junk\nMemAvailable: 2 kB\n", "MemTotal: 4096 kB\nMemAvailable: 2 kB\nMemAvailable: 3 kB\n",
             "MemTotal: 4096 kB\nMemTotal: 5000 kB\nMemAvailable: 2 kB\n",
             "MemTotal: 18446744073709551615 kB\nMemAvailable: 2 kB\n"})
    {
        stats = parse(text);
        require(!stats.ram_total_bytes && !stats.ram_free_bytes, "invalid sample published partial data");
    }
    require(!parse(std::string(70000, 'x')).ram_total_bytes, "oversized input accepted");
    require(!parse("MemTotal: " + std::to_string(std::numeric_limits<size_t>::max() / 1024 + 1) + " kB\nMemAvailable: 0 kB\n").ram_total_bytes,
            "native size overflow accepted");
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, release = false;
    std::atomic<unsigned> samples{0};
    {
        MemoryMonitor monitor([&]
                              {
            ++samples;
            std::unique_lock<std::mutex> lock(mutex);
            entered = true;
            changed.notify_all();
            changed.wait(lock, [&] { return release; });
            return parse("MemTotal: 4096 kB\nMemAvailable: 1000 kB\n"); },
                              std::chrono::milliseconds(10));
        {
            std::unique_lock<std::mutex> lock(mutex);
            require(changed.wait_for(lock, std::chrono::seconds(2), [&]
                                     { return entered; }),
                    "sampler not entered");
        }
        // A slow reader owns no snapshot mutex and cannot block UI getters.
        for (unsigned i = 0; i < 10000; ++i) require(!monitor.snapshot().ram_total_bytes, "unpublished sample leaked");
        require(samples == 1, "snapshot performed additional samples");
        {
            std::lock_guard<std::mutex> lock(mutex);
            release = true;
        }
        changed.notify_all();
        await([&]
              { return monitor.snapshot().ram_free_bytes == 1000 * 1024; });
    }
    {
        std::atomic<bool> bad{false};
        MemoryMonitor monitor([&]() -> platform::ui::device::MemoryStats
                              {
            if (bad) throw std::runtime_error("simulated read failure");
            return parse("MemTotal: 4096 kB\nMemAvailable: 1000 kB\n"); },
                              std::chrono::milliseconds(10));
        await([&]
              { return monitor.snapshot().ram_total_bytes != 0; });
        bad = true;
        await([&]
              { return monitor.snapshot().ram_total_bytes == 0; });
    }
    await([]
          { return platform::ui::device::memory_stats().ram_total_bytes != 0; });
    stats = platform::ui::device::memory_stats();
    require(stats.ram_free_bytes <= stats.ram_total_bytes, "live Linux sample inconsistent");
}

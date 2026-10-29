#include "platform/linux/memory_monitor.h"
#include <algorithm>
#include <charconv>
#include <condition_variable>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>

namespace platform::linux_runtime
{
namespace
{
using Stats = platform::ui::device::MemoryStats;
std::optional<size_t> kilobytes(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos) return {};
    text.remove_prefix(first);
    uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr == text.data() || parsed.ptr == text.data() + text.size()) return {};
    std::string_view suffix(parsed.ptr, text.data() + text.size() - parsed.ptr);
    if (suffix.front() != ' ' && suffix.front() != '\t') return {};
    const auto unit = suffix.find_first_not_of(" \t");
    if (unit == std::string_view::npos) return {};
    suffix.remove_prefix(unit);
    if (suffix.substr(0, 2) != "kB") return {};
    suffix.remove_prefix(2);
    if (suffix.find_first_not_of(" \t\r") != std::string_view::npos || value > std::numeric_limits<size_t>::max() / 1024) return {};
    return static_cast<size_t>(value) * 1024;
}
Stats sample()
{
    std::ifstream input("/proc/meminfo");
    return parse_linux_meminfo(input);
}
} // namespace

Stats parse_linux_meminfo(std::istream& input)
{
    std::optional<size_t> total, available;
    char buffer[512];
    size_t bytes = 0;
    while (input.getline(buffer, sizeof(buffer)))
    {
        const std::string_view line(buffer, static_cast<size_t>(input.gcount() - (input.eof() ? 0 : 1)));
        if ((bytes += static_cast<size_t>(input.gcount())) > 65536 || line.find('\0') != std::string_view::npos) return {};
        if (line.substr(0, 9) == "MemTotal:")
        {
            if (total) return {};
            total = kilobytes(line.substr(9));
            if (!total || !*total) return {};
        }
        else if (line.substr(0, 13) == "MemAvailable:")
        {
            if (available) return {};
            available = kilobytes(line.substr(13));
            if (!available) return {};
        }
    }
    if (!input.eof() || !total || !available || *available > *total) return {};
    Stats out;
    out.ram_total_bytes = *total;
    out.ram_free_bytes = *available;
    return out;
}

struct MemoryMonitor::Impl
{
    Impl(Sampler value, std::chrono::milliseconds interval)
        : sampler(value ? std::move(value) : sample), period(std::max(interval, std::chrono::milliseconds(10))),
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
            Stats next;
            try
            {
                next = sampler();
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
    Sampler sampler;
    std::chrono::milliseconds period;
    mutable std::mutex mutex;
    std::condition_variable wake;
    Stats current;
    bool stopping = false;
    std::thread worker;
};
MemoryMonitor::MemoryMonitor(Sampler sampler, std::chrono::milliseconds interval)
    : impl_(std::make_unique<Impl>(std::move(sampler), interval)) {}
MemoryMonitor::~MemoryMonitor() = default;
Stats MemoryMonitor::snapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->current;
}
Stats linux_memory_snapshot()
{
    static MemoryMonitor monitor;
    return monitor.snapshot();
}
} // namespace platform::linux_runtime

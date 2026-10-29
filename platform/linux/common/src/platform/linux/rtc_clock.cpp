#include "platform/linux/rtc_clock.h"
#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace platform::linux_runtime
{
namespace
{
std::string line(const std::filesystem::path& path)
{
    std::ifstream input(path);
    std::string value;
    if (!std::getline(input, value)) return {};
    if (!value.empty() && value.back() == '\r') value.pop_back();
    return value;
}
bool rtc_class_name(const std::string& name)
{
    return name.size() > 3 && name.compare(0, 3, "rtc") == 0 &&
           std::all_of(name.begin() + 3, name.end(), [](char ch)
                       { return ch >= '0' && ch <= '9'; });
}
} // namespace

RtcSnapshot read_rtc_clock(const std::filesystem::path& root, const std::string& selector)
{
    std::vector<RtcSnapshot> candidates;
    std::error_code error;
    for (std::filesystem::directory_iterator it(root, error), end; !error && it != end; it.increment(error))
    {
        const auto name = it->path().filename().string();
        if (!rtc_class_name(name)) continue;
        RtcSnapshot sample;
        sample.class_name = name;
        sample.device_name = line(it->path() / "device/name");
        const auto kernel_name = line(it->path() / "name");
        if (!selector.empty() && selector != name && selector != sample.device_name && selector != kernel_name) continue;
        if (kernel_name.empty()) continue;
        sample.available = true;
        sample.system_clock_source = line(it->path() / "hctosys") == "1";
        std::ifstream epoch(it->path() / "since_epoch");
        if (epoch >> sample.utc_seconds)
        {
            epoch >> std::ws;
            sample.valid = epoch.eof() && sample.utc_seconds >= 0;
        }
        if (!sample.valid) sample.error = "RTC time is invalid or unreadable";
        candidates.push_back(std::move(sample));
    }
    if (error) return {false, false, false, 0, {}, {}, "RTC class storage is unavailable"};
    if (candidates.empty()) return {false, false, false, 0, {}, {}, "No matching RTC device"};
    if (candidates.size() == 1) return candidates.front();
    if (selector.empty())
    {
        const auto count = std::count_if(candidates.begin(), candidates.end(), [](const auto& rtc)
                                         { return rtc.system_clock_source; });
        if (count == 1)
            return *std::find_if(candidates.begin(), candidates.end(), [](const auto& rtc)
                                 { return rtc.system_clock_source; });
    }
    return {false, false, false, 0, {}, {}, "Multiple RTC devices match; select one explicitly"};
}

struct RtcMonitor::Impl
{
    std::filesystem::path root;
    std::string selector;
    std::chrono::milliseconds interval;
    mutable std::mutex mutex;
    std::condition_variable wake;
    bool ending = false;
    RtcSnapshot cached;
    std::chrono::steady_clock::time_point updated{};
    std::thread worker;
    Impl(std::filesystem::path path, std::string selection, std::chrono::milliseconds period)
        : root(std::move(path)), selector(std::move(selection)), interval(std::max(period, std::chrono::milliseconds(10))),
          worker([this]
                 { run(); }) {}
    ~Impl()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            ending = true;
        }
        wake.notify_one();
        worker.join();
    }
    void run()
    {
        for (;;)
        {
            auto sample = read_rtc_clock(root, selector);
            std::unique_lock<std::mutex> lock(mutex);
            if (ending) return;
            cached = std::move(sample);
            updated = std::chrono::steady_clock::now();
            if (wake.wait_for(lock, interval, [&]
                              { return ending; })) return;
        }
    }
};
RtcMonitor::RtcMonitor(std::filesystem::path root, std::string selector, std::chrono::milliseconds interval)
    : impl_(std::make_unique<Impl>(std::move(root), std::move(selector), interval)) {}
RtcMonitor::~RtcMonitor() = default;
RtcSnapshot RtcMonitor::snapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto result = impl_->cached;
    if (std::chrono::steady_clock::now() - impl_->updated > impl_->interval * 3)
    {
        result.valid = false;
        result.error = "RTC sample is not fresh";
    }
    return result;
}
RtcSnapshot linux_rtc_snapshot()
{
    static RtcMonitor monitor("/sys/class/rtc", []
                              { const char* value = std::getenv("TRAIL_MATE_RTC_NAME"); return value ? value : ""; }());
    return monitor.snapshot();
}
} // namespace platform::linux_runtime

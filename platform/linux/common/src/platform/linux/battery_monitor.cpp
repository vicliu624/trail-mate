#include "platform/linux/battery_monitor.h"
#include <algorithm>
#include <charconv>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace platform::linux_runtime
{
namespace
{
using Info = ::platform::ui::device::BatteryInfo;
std::optional<std::string> line(const std::filesystem::path& path)
{
    std::ifstream stream(path);
    char buffer[256]{};
    if (!stream.getline(buffer, sizeof(buffer))) return std::nullopt;
    std::string text(buffer);
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::nullopt;
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
std::optional<int> integer(const std::filesystem::path& path)
{
    const auto text = line(path);
    if (!text) return std::nullopt;
    int value = 0;
    const auto parsed = std::from_chars(text->data(), text->data() + text->size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text->data() + text->size()) return std::nullopt;
    return value;
}
} // namespace
Info read_power_supplies(const std::filesystem::path& root, const std::string& battery_name)
{
    Info out;
    std::error_code error;
    std::vector<std::filesystem::path> supplies;
    std::filesystem::directory_iterator iterator(root, error), end;
    while (!error && iterator != end)
    {
        supplies.push_back(iterator->path());
        iterator.increment(error);
    }
    if (error) return out;
    std::sort(supplies.begin(), supplies.end());
    size_t system_batteries = 0, charger_statuses = 0;
    std::string battery_status, charger_status;
    for (const auto& path : supplies)
    {
        const auto type = line(path / "type");
        if (!type || line(path / "scope").value_or("") == "Device") continue;
        if (*type == "Battery")
        {
            if (integer(path / "present").value_or(1) != 1) continue;
            ++system_batteries;
            if (!battery_name.empty() && path.filename().string() != battery_name) continue;
            if (out.available) continue;
            const auto capacity = integer(path / "capacity");
            if (!capacity || *capacity < 0 || *capacity > 100) continue;
            out.available = true;
            out.level = *capacity;
            battery_status = line(path / "status").value_or("");
        }
        else if (integer(path / "online").value_or(0) == 1)
        {
            out.external_power = true;
            const auto status = line(path / "status");
            if (status && (*status == "Charging" || *status == "Full" || *status == "Not charging" || *status == "Discharging"))
            {
                ++charger_statuses;
                charger_status = *status;
            }
        }
    }
    // A separate gauge may not know the charger phase. Only resolve that case
    // when the topology is unambiguous; never apply one charger's phase to an
    // arbitrarily selected pack on a multi-battery Linux device.
    if (out.available && system_batteries == 1 && charger_statuses == 1) battery_status = charger_status;
    out.charging = out.available && battery_status == "Charging";
    return out;
}
struct BatteryMonitor::Impl
{
    Impl(std::filesystem::path path, std::string preferred, std::chrono::milliseconds interval)
        : root(std::move(path)), name(std::move(preferred)), period(std::max(interval, std::chrono::milliseconds(10))),
          worker([this]
                 { run(); }) {}
    ~Impl()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        worker.join();
    }
    void run()
    {
        std::unique_lock<std::mutex> lock(mutex);
        while (!stopping)
        {
            lock.unlock();
            Info value;
            try
            {
                value = read_power_supplies(root, name);
            }
            catch (...)
            {
                value = {};
            }
            lock.lock();
            current = value;
            wake.wait_for(lock, period, [this]
                          { return stopping; });
        }
    }
    std::filesystem::path root;
    std::string name;
    std::chrono::milliseconds period;
    mutable std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false;
    Info current;
    std::thread worker;
};
BatteryMonitor::BatteryMonitor(std::filesystem::path root, std::string name, std::chrono::milliseconds period)
    : impl_(std::make_unique<Impl>(std::move(root), std::move(name), period)) {}
BatteryMonitor::~BatteryMonitor() = default;
Info BatteryMonitor::snapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->current;
}
Info linux_battery_snapshot()
{
    const auto env = [](const char* name, const char* fallback)
    {
        const char* value = std::getenv(name);
        return value && *value ? value : fallback;
    };
    static BatteryMonitor monitor(env("TRAIL_MATE_POWER_SUPPLY_ROOT", "/sys/class/power_supply"), env("TRAIL_MATE_BATTERY_DEVICE", ""));
    return monitor.snapshot();
}
} // namespace platform::linux_runtime

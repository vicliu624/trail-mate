#pragma once
#include "platform/ui/device_runtime.h"
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

namespace platform::linux_runtime
{
// Blocking sysfs adapter, called by the storage/monitor thread, never by LVGL.
::platform::ui::device::BatteryInfo read_power_supplies(const std::filesystem::path& root, const std::string& battery_name = {});
class BatteryMonitor
{
  public:
    explicit BatteryMonitor(std::filesystem::path root, std::string battery_name = {},
                            std::chrono::milliseconds interval = std::chrono::seconds(1));
    ~BatteryMonitor();
    ::platform::ui::device::BatteryInfo snapshot() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
::platform::ui::device::BatteryInfo linux_battery_snapshot();
} // namespace platform::linux_runtime

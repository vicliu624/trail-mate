#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace platform::linux_runtime
{
struct RtcSnapshot
{
    bool available = false;
    bool valid = false;
    bool system_clock_source = false;
    int64_t utc_seconds = 0;
    std::string class_name;
    std::string device_name;
    std::string error;
};
// Blocking sysfs access for the monitor owner; no chip names or bus IDs assumed.
RtcSnapshot read_rtc_clock(const std::filesystem::path& root, const std::string& selector = {});
class RtcMonitor
{
  public:
    explicit RtcMonitor(std::filesystem::path root, std::string selector = {},
                        std::chrono::milliseconds interval = std::chrono::seconds(1));
    ~RtcMonitor();
    RtcSnapshot snapshot() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
RtcSnapshot linux_rtc_snapshot();
} // namespace platform::linux_runtime

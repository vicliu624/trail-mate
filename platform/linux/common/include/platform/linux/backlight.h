#pragma once
#include "platform/linux/gamma_control.h"
#include <cstdint>
#include <filesystem>
#include <memory>

namespace platform::linux_runtime
{
struct BacklightOptions
{
    std::string backend = "sysfs";
    GammaControlOptions gamma;
    std::filesystem::path root = "/sys/class/backlight";
    std::filesystem::path device;
};
struct BacklightSnapshot
{
    bool available = false, selection_required = false, failed = false;
    uint8_t level = 0;
    uint32_t native_maximum = 0;
};
class BacklightRuntime
{
  public:
    explicit BacklightRuntime(BacklightOptions options);
    ~BacklightRuntime();
    BacklightSnapshot snapshot() const;
    bool request(uint8_t level);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
bool configure_backlight(const BacklightOptions& options);
void prepare_backlight();
BacklightSnapshot backlight_snapshot();
bool request_backlight(uint8_t level);
void shutdown_backlight();
} // namespace platform::linux_runtime

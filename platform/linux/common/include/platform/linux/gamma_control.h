#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace platform::linux_runtime
{
struct GammaControlOptions
{
    // Empty socket uses WAYLAND_DISPLAY in XDG_RUNTIME_DIR. No output index.
    std::filesystem::path socket;
    std::string output;
};

enum class GammaControlStatus
{
    Ready,
    Unavailable,
    SelectionRequired,
    Failed
};

// Owned by a platform worker, never the UI thread. Destroying the connection
// releases exclusive gamma ownership and restores the compositor's original LUT.
// This is software attenuation, not a panel/backlight hardware control.
class GammaControl
{
  public:
    GammaControl();
    ~GammaControl();
    GammaControlStatus connect(const GammaControlOptions& options, const std::atomic_bool& cancel);
    bool apply(uint8_t level, const std::atomic_bool& cancel);
    bool healthy(const std::atomic_bool& cancel);
    void close();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime

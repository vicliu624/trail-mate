#pragma once

#include <string>
#include <string_view>

namespace trailmate::uconsole
{

struct UConsoleHardwareProbe
{
    bool aio2_detected = false;
    bool terminal_usb_detected = false;
    bool gps_power_control_available = false;
    bool gps_powered = false;
    bool lora_spi_detected = false;
    bool lora_binding_configured = false;
    bool i2c_detected = false;

    std::string terminal_usb_path{};
    std::string lora_spi_path{};
    std::string i2c_summary{};
    std::string summary{};
};

[[nodiscard]] UConsoleHardwareProbe probeUConsoleHardware();

struct UConsoleGpsSourceSettings
{
    // False means no uConsole UI choice exists: retain an explicit source
    // supplied by a headless/operator launch.
    bool is_configured = false;
    std::string device_path{};
    int baud = 9600;
};

// The uConsole USB CDC port is a control-plane endpoint. It is never a valid
// AIO2 GNSS receiver path, even though it is a serial device.
[[nodiscard]] bool isAllowedUConsoleGpsDevicePath(
    std::string_view device_path) noexcept;
[[nodiscard]] UConsoleGpsSourceSettings loadUConsoleGpsSourceSettings();
void saveUConsoleGpsSourceSettings(
    const UConsoleGpsSourceSettings& settings);
void applyUConsoleGpsSourceSettings(
    const UConsoleGpsSourceSettings& settings);
[[nodiscard]] bool isUsingUConsoleAio2DefaultGps() noexcept;
[[nodiscard]] int uConsoleAio2DefaultGpsBaud() noexcept;

// Owns only the Linux/AIO2 endpoint binding.  It deliberately does not own
// GPS parsing, radio protocol policy, or any screen state.
class UConsoleHardwareRuntime final
{
  public:
    UConsoleHardwareRuntime() = default;
    ~UConsoleHardwareRuntime();

    UConsoleHardwareRuntime(const UConsoleHardwareRuntime&) = delete;
    UConsoleHardwareRuntime& operator=(const UConsoleHardwareRuntime&) = delete;

    [[nodiscard]] bool initialize();
    void shutdown();
    [[nodiscard]] bool gpsPowerIsOn() const noexcept;
    [[nodiscard]] const char* lastError() const noexcept;

  private:
    int gps_power_fd_ = -1;
    bool initialized_ = false;
    char last_error_[192]{};
};

} // namespace trailmate::uconsole

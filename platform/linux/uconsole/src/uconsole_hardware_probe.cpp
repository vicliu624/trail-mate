#include "uconsole/uconsole_hardware_probe.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include "platform/ui/settings_store.h"

namespace trailmate::uconsole
{
namespace
{

namespace fs = std::filesystem;

constexpr const char* kAio2SpiDevice = "/dev/spidev1.0";
constexpr const char* kAio2GpioChip = "/dev/gpiochip0";
constexpr const char* kAio2GpsDevice = "/dev/ttyS0";
constexpr int kAio2GpsPowerGpio = 27;
constexpr int kAio2GpsBaud = 9600;
constexpr const char* kGpsSettingsNamespace = "uconsole_gps";
constexpr const char* kGpsDeviceKey = "receiver_device";
constexpr const char* kGpsBaudKey = "receiver_baud";
constexpr const char* kAio2DefaultGpsEnv =
    "TRAIL_MATE_UCONSOLE_AIO2_GPS_DEFAULT";

[[nodiscard]] bool containsToken(const std::string& text,
                                 const char* token) noexcept
{
    return text.find(token) != std::string::npos;
}

[[nodiscard]] std::string pathString(const fs::path& path)
{
    return path.string();
}

[[nodiscard]] bool existingPath(const fs::path& path)
{
    std::error_code ec;
    return fs::exists(path, ec) && !ec;
}

[[nodiscard]] std::vector<fs::path> directoryEntries(const fs::path& dir)
{
    std::vector<fs::path> out{};
    std::error_code ec;
    if (!fs::exists(dir, ec) || ec)
    {
        return out;
    }

    for (const auto& entry : fs::directory_iterator(dir, ec))
    {
        if (ec)
        {
            break;
        }
        out.push_back(entry.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

[[nodiscard]] fs::path resolvedDevicePath(const fs::path& path)
{
    std::error_code ec;
    fs::path resolved = fs::canonical(path, ec);
    if (ec)
    {
        return path;
    }
    return resolved;
}

[[nodiscard]] bool findClockworkPiTerminalUsb(fs::path& out_path)
{
    for (const auto& path : directoryEntries("/dev/serial/by-id"))
    {
        const std::string name = path.filename().string();
        if (containsToken(name, "ClockworkPI") &&
            containsToken(name, "uConsole"))
        {
            out_path = path;
            return true;
        }
    }

    return false;
}

[[nodiscard]] bool findSpiDevice(fs::path& out_path)
{
    if (const char* configured = std::getenv("TRAIL_MATE_LORA_SPI");
        configured != nullptr && configured[0] != '\0' &&
        existingPath(configured))
    {
        out_path = configured;
        return true;
    }

    if (existingPath(kAio2SpiDevice))
    {
        out_path = kAio2SpiDevice;
        return true;
    }
    return false;
}

[[nodiscard]] bool hasAio2GpioControlPlane()
{
    return existingPath(kAio2GpioChip);
}

[[nodiscard]] bool envIsEnabled(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr && value[0] == '1' && value[1] == '\0';
}

[[nodiscard]] bool envConfigured(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0';
}

void setEnvDefault(const char* name, const char* value)
{
    if (std::getenv(name) == nullptr || std::getenv(name)[0] == '\0')
    {
#if defined(_WIN32)
        _putenv_s(name, value);
#else
        setenv(name, value, 0);
#endif
    }
}

void setEnvValue(const char* name, const char* value)
{
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void clearEnvValue(const char* name)
{
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

[[nodiscard]] bool isSupportedGpsBaud(int baud) noexcept
{
    switch (baud)
    {
    case 4800:
    case 9600:
    case 19200:
    case 38400:
    case 57600:
    case 115200:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool aio2DefaultGpsAvailable()
{
    return existingPath(kAio2GpsDevice) && hasAio2GpioControlPlane();
}

[[nodiscard]] bool hasExplicitGpsSourceOverride(bool ignore_device)
{
    return (!ignore_device && envConfigured("TRAIL_MATE_GPS_DEVICE")) ||
           envConfigured("TRAIL_MATE_GPS_NMEA_FILE") ||
           envConfigured("TRAIL_MATE_GPS_VALID") ||
           envConfigured("TRAIL_MATE_GPS_LAT") ||
           envConfigured("TRAIL_MATE_GPS_LNG");
}

void configureAio2RadioEnvironment()
{
    // Keep operator overrides intact, but never let generic Linux defaults
    // select a random spidev node on uConsole.
    setEnvDefault("TRAIL_MATE_LORA_SPI", kAio2SpiDevice);
    setEnvDefault("TRAIL_MATE_LORA_GPIOCHIP", kAio2GpioChip);
    setEnvDefault("TRAIL_MATE_LORA_POWER_GPIO", "16");
    setEnvDefault("TRAIL_MATE_LORA_RESET_GPIO", "25");
    setEnvDefault("TRAIL_MATE_LORA_BUSY_GPIO", "24");
    setEnvDefault("TRAIL_MATE_LORA_IRQ_GPIO", "26");
    setEnvDefault("TRAIL_MATE_LORA_DIO2_RF_SWITCH", "1");
    setEnvDefault("TRAIL_MATE_LORA_DIO3_TCXO_1V8", "1");
}

void disableUnsafeGpsAutoprobe()
{
    // The uConsole USB CDC device is its keyboard/control-plane serial link,
    // not the AIO2 GNSS receiver. The uConsole binding selects the verified
    // AIO2 UART explicitly; generic candidate probing must not add USB CDC.
    // This is intentionally not a default: a process inherited with this set
    // to 1 would otherwise put the uConsole control port back into the shared
    // Linux candidate list.
    setEnvValue("TRAIL_MATE_GPS_AUTO_SERIAL", "0");
}

#if defined(__linux__)
[[nodiscard]] bool requestOutputGpio(const char* chip_path,
                                     int offset,
                                     int value,
                                     const char* label,
                                     int* out_fd,
                                     char* error,
                                     std::size_t error_size)
{
    *out_fd = -1;
    const int chip_fd = open(chip_path, O_RDONLY | O_CLOEXEC);
    if (chip_fd < 0)
    {
        std::snprintf(error, error_size, "open %s failed: %s", chip_path,
                      std::strerror(errno));
        return false;
    }

    gpiohandle_request request{};
    request.lineoffsets[0] = static_cast<unsigned>(offset);
    request.lines = 1;
    request.flags = GPIOHANDLE_REQUEST_OUTPUT;
    request.default_values[0] = value != 0 ? 1 : 0;
    std::snprintf(request.consumer_label, sizeof(request.consumer_label), "%s",
                  label);
    const bool ok = ioctl(chip_fd, GPIO_GET_LINEHANDLE_IOCTL, &request) == 0;
    if (!ok)
    {
        std::snprintf(error, error_size, "request GPIO%d failed: %s", offset,
                      std::strerror(errno));
    }
    close(chip_fd);
    if (!ok)
    {
        return false;
    }
    *out_fd = request.fd;
    return true;
}
#endif

[[nodiscard]] std::string summarizeI2c()
{
    std::vector<std::string> names{};
    for (const auto& path : directoryEntries("/dev"))
    {
        const std::string name = path.filename().string();
        if (name.rfind("i2c-", 0) == 0)
        {
            names.push_back("/dev/" + name);
        }
    }

    if (names.empty())
    {
        return {};
    }

    std::ostringstream out;
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        if (index != 0)
        {
            out << ", ";
        }
        out << names[index];
    }
    return out.str();
}

} // namespace

bool isAllowedUConsoleGpsDevicePath(std::string_view device_path) noexcept
{
    if (device_path.empty())
    {
        return true;
    }

    constexpr std::string_view kDevicePrefix = "/dev/";
    return device_path.starts_with(kDevicePrefix) &&
           device_path.find("ttyACM") == std::string_view::npos &&
           device_path.find("ClockworkPI") == std::string_view::npos &&
           device_path.find("uConsole") == std::string_view::npos;
}

UConsoleGpsSourceSettings loadUConsoleGpsSourceSettings()
{
    UConsoleGpsSourceSettings settings{};
    settings.is_configured = ::platform::ui::settings_store::get_string(
        kGpsSettingsNamespace, kGpsDeviceKey, settings.device_path);
    settings.baud = ::platform::ui::settings_store::get_int(
        kGpsSettingsNamespace, kGpsBaudKey, settings.baud);
    if (!isAllowedUConsoleGpsDevicePath(settings.device_path) ||
        settings.device_path.empty())
    {
        settings.device_path.clear();
        settings.is_configured = false;
        settings.baud = kAio2GpsBaud;
    }
    if (!isSupportedGpsBaud(settings.baud))
    {
        settings.baud = 9600;
    }
    return settings;
}

void saveUConsoleGpsSourceSettings(const UConsoleGpsSourceSettings& settings)
{
    const std::string device_path =
        isAllowedUConsoleGpsDevicePath(settings.device_path)
            ? settings.device_path
            : std::string{};
    const int baud = isSupportedGpsBaud(settings.baud) ? settings.baud : 9600;
    (void)::platform::ui::settings_store::put_string(
        kGpsSettingsNamespace, kGpsDeviceKey, device_path.c_str());
    ::platform::ui::settings_store::put_int(kGpsSettingsNamespace,
                                            kGpsBaudKey, baud);
}

void applyUConsoleGpsSourceSettings(const UConsoleGpsSourceSettings& settings)
{
    const bool was_aio2_default = envIsEnabled(kAio2DefaultGpsEnv);
    clearEnvValue(kAio2DefaultGpsEnv);
    disableUnsafeGpsAutoprobe();

    // A process-level source is deliberately stronger than a persisted UI
    // choice. This makes scripted NMEA replay and emergency location injection
    // deterministic, while retaining the uConsole default for ordinary runs.
    if (hasExplicitGpsSourceOverride(was_aio2_default))
    {
        if (was_aio2_default)
        {
            clearEnvValue("TRAIL_MATE_GPS_DEVICE");
        }
        return;
    }

    if (settings.is_configured &&
        isAllowedUConsoleGpsDevicePath(settings.device_path) &&
        !settings.device_path.empty())
    {
        setEnvValue("TRAIL_MATE_GPS_DEVICE", settings.device_path.c_str());
        const std::string baud = std::to_string(
            isSupportedGpsBaud(settings.baud) ? settings.baud : kAio2GpsBaud);
        setEnvValue("TRAIL_MATE_GPS_BAUD", baud.c_str());
        return;
    }

    if (!aio2DefaultGpsAvailable())
    {
        return;
    }

    setEnvValue("TRAIL_MATE_GPS_DEVICE", kAio2GpsDevice);
    setEnvValue("TRAIL_MATE_GPS_BAUD", "9600");
    setEnvValue(kAio2DefaultGpsEnv, "1");
}

bool isUsingUConsoleAio2DefaultGps() noexcept
{
    return envIsEnabled(kAio2DefaultGpsEnv);
}

int uConsoleAio2DefaultGpsBaud() noexcept
{
    return kAio2GpsBaud;
}

UConsoleHardwareProbe probeUConsoleHardware()
{
    UConsoleHardwareProbe out{};

    fs::path serial_path{};
    if (findClockworkPiTerminalUsb(serial_path))
    {
        out.terminal_usb_detected = true;
        out.terminal_usb_path = pathString(resolvedDevicePath(serial_path));
    }

    fs::path spi_path{};
    if (findSpiDevice(spi_path))
    {
        out.lora_spi_detected = true;
        out.lora_spi_path = pathString(spi_path);
    }
    out.gps_power_control_available = hasAio2GpioControlPlane();
    out.gps_powered = envIsEnabled("TRAIL_MATE_GPS_POWERED");
    out.lora_binding_configured = out.lora_spi_detected &&
                                  out.gps_power_control_available;
    out.aio2_detected = out.lora_binding_configured ||
                        out.gps_power_control_available;

    out.i2c_summary = summarizeI2c();
    out.i2c_detected = !out.i2c_summary.empty();

    std::ostringstream summary;
    summary << (out.aio2_detected ? "AIO2 endpoints present"
                                  : "No AIO2 endpoint detected");
    if (!out.terminal_usb_path.empty())
    {
        summary << " / uConsole USB " << out.terminal_usb_path;
    }
    if (!out.lora_spi_path.empty())
    {
        summary << " / SPI " << out.lora_spi_path;
    }
    if (out.gps_power_control_available)
    {
        summary << " / GPS power " << (out.gps_powered ? "on" : "off");
    }
    if (out.i2c_detected)
    {
        summary << " / I2C " << out.i2c_summary;
    }
    out.summary = summary.str();
    return out;
}

UConsoleHardwareRuntime::~UConsoleHardwareRuntime()
{
    shutdown();
}

bool UConsoleHardwareRuntime::initialize()
{
    if (initialized_)
    {
        return true;
    }

    last_error_[0] = '\0';
    configureAio2RadioEnvironment();
    disableUnsafeGpsAutoprobe();
    applyUConsoleGpsSourceSettings(loadUConsoleGpsSourceSettings());
    // Publish physical state to the shared GPS runtime and hardware screen.
    setEnvValue("TRAIL_MATE_GPS_POWERED", "0");

#if defined(__linux__)
    if (!requestOutputGpio(kAio2GpioChip, kAio2GpsPowerGpio, 1,
                           "trailmate-aio2-gps-power", &gps_power_fd_,
                           last_error_, sizeof(last_error_)))
    {
        return false;
    }
#endif
    setEnvValue("TRAIL_MATE_GPS_POWERED", "1");
    initialized_ = true;
    return true;
}

void UConsoleHardwareRuntime::shutdown()
{
#if defined(__linux__)
    if (gps_power_fd_ >= 0)
    {
        gpiohandle_data values{};
        values.values[0] = 0;
        (void)ioctl(gps_power_fd_, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &values);
        close(gps_power_fd_);
        gps_power_fd_ = -1;
    }
#endif
    setEnvValue("TRAIL_MATE_GPS_POWERED", "0");
    initialized_ = false;
}

bool UConsoleHardwareRuntime::gpsPowerIsOn() const noexcept
{
    return initialized_;
}

const char* UConsoleHardwareRuntime::lastError() const noexcept
{
    return last_error_;
}

} // namespace trailmate::uconsole

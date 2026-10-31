#include "platform/desktop/device_options.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#if defined(__linux__)
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace platform::desktop
{
namespace
{
std::string trim(const std::string& text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

int integer(const std::string& value)
{
    std::size_t used = 0;
    const int result = std::stoi(value, &used);
    if (used != value.size()) throw std::runtime_error("Invalid integer: " + value);
    return result;
}
} // namespace

void validateDeviceOptions(const DeviceOptions& options)
{
    if (options.backlight.backend != "sysfs" && options.backlight.backend != "wayland_gamma")
        throw std::runtime_error("Backlight backend must be sysfs or wayland_gamma");
    if ((!options.backlight.gamma.socket.empty() && !options.backlight.gamma.socket.is_absolute()) ||
        options.backlight.gamma.output.size() > 255 ||
        options.backlight.gamma.output.find_first_of("\r\n\t\0", 0, 4) != std::string::npos ||
        (options.backlight.backend == "sysfs" &&
         (!options.backlight.gamma.socket.empty() || !options.backlight.gamma.output.empty())))
        throw std::runtime_error("Invalid Wayland gamma output/socket selection");
    if (!platform::linux_runtime::valid_keyboard_backlight_options(options.keyboard_backlight))
        throw std::runtime_error("Invalid keyboard backlight PWM configuration");
    if (options.screen_power != "none" && options.screen_power != "wayland")
        throw std::runtime_error("Screen power must be none or wayland");
    if (options.screen_power_output.size() > 255 ||
        options.screen_power_output.find_first_of("\r\n\t\0", 0, 4) != std::string::npos ||
        (options.screen_power == "none" && !options.screen_power_output.empty()))
        throw std::runtime_error("Invalid screen power output selection");
    if (!options.backlight.root.is_absolute() ||
        (!options.backlight.device.empty() && !options.backlight.device.is_absolute()))
        throw std::runtime_error("Backlight paths must be absolute");
    if (!platform::linux_runtime::valid_gnss_source_options(options.gnss))
        throw std::runtime_error("Invalid GNSS protocol/device/baud configuration");
    if (options.gnss.power.line >= 0 && options.radio.driver != "none" && options.gnss.power.chip == options.radio.bus.gpiochip)
        for (const int line : {options.radio.bus.reset_gpio, options.radio.bus.busy_gpio, options.radio.bus.irq_gpio, options.radio.bus.power_gpio})
            if (line == options.gnss.power.line) throw std::runtime_error("GNSS power and radio GPIO lines must be distinct");
    if (options.width <= 0 || options.height <= 0 ||
        options.width > 8192 || options.height > 8192)
        throw std::runtime_error("Device width and height must be in 1..8192");
    if (options.rotation != 0 && options.rotation != 90 &&
        options.rotation != 180 && options.rotation != 270)
        throw std::runtime_error("Device rotation must be 0, 90, 180 or 270");
    if (options.ux_pack.empty()) throw std::runtime_error("Device ux_pack is required");
    if (options.radio.driver != "none" && options.radio.driver != "sx126x" && options.radio.driver != "lr2021")
        throw std::runtime_error("Unknown packet radio driver");
    using RfProfile = platform::linux_runtime::Lr2021RfSwitchProfile;
    if ((options.radio.rf_switch_profile != RfProfile::None && options.radio.rf_switch_profile != RfProfile::Cm0_16E8) ||
        (options.radio.rf_switch_profile != RfProfile::None && options.radio.driver != "lr2021"))
        throw std::runtime_error("RF switch profile requires the matching LR2021 backend");
    if (options.radio.driver != "none" &&
        (options.radio.bus.reset_gpio < 0 || options.radio.bus.busy_gpio < 0 ||
         options.radio.bus.reset_gpio == options.radio.bus.busy_gpio ||
         options.radio.bus.spi_speed_hz < 1000 || options.radio.bus.spi_speed_hz > 16000000))
        throw std::runtime_error("Invalid packet radio GPIO/SPI configuration");
    if (!std::isfinite(options.radio.tcxo_voltage) || options.radio.tcxo_voltage < 0 || options.radio.tcxo_voltage > 3.3f)
        throw std::runtime_error("Invalid radio TCXO voltage");
    if (options.radio.driver != "none")
    {
        const int pins[] = {options.radio.bus.reset_gpio, options.radio.bus.busy_gpio,
                            options.radio.bus.irq_gpio, options.radio.bus.power_gpio};
        for (unsigned first = 0; first < 4; ++first)
            for (unsigned second = first + 1; second < 4; ++second)
                if (pins[first] >= 0 && pins[first] == pins[second])
                    throw std::runtime_error("Radio GPIO lines must be distinct");
    }
    if (options.sensors.mount_rotation < 0 || options.sensors.mount_rotation >= 360 || options.sensors.mount_rotation % 90)
        throw std::runtime_error("IMU mount rotation must be 0, 90, 180 or 270");
    for (const auto* path : {&options.paths.settings_root, &options.paths.sd_root, &options.paths.cache_root})
        if (!path->empty() && !path->is_absolute()) throw std::runtime_error("Application directory overrides must be absolute");
}

DeviceOptions loadDeviceOptions(const std::string& path)
{
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open device profile: " + path);
    DeviceOptions options;
    std::string line;
    while (std::getline(input, line))
    {
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        const auto equal = line.find('=');
        if (equal == std::string::npos) throw std::runtime_error("Invalid profile line: " + line);
        const auto key = trim(line.substr(0, equal));
        const auto value = trim(line.substr(equal + 1));
        if (key == "title") options.title = value;
        else if (key == "runtime_mode")
        {
            using Mode = platform::linux_runtime::LinuxRuntimeMode;
            if (value == "mesh") options.runtime_mode = Mode::DeviceRealMesh;
            else if (value == "local") options.runtime_mode = Mode::DeviceLocal;
            else if (value == "demo") options.runtime_mode = Mode::SimulatorDemo;
            else throw std::runtime_error("Unknown Linux runtime mode");
        }
        else if (key == "ux_pack") options.ux_pack = value;
        else if (key == "width") options.width = integer(value);
        else if (key == "height") options.height = integer(value);
        else if (key == "rotation") options.rotation = integer(value);
        else if (key == "state_root") options.paths.settings_root = value;
        else if (key == "data_root") options.paths.sd_root = value;
        else if (key == "cache_root") options.paths.cache_root = value;
        else if (key == "backlight_backend") options.backlight.backend = value;
        else if (key == "backlight_output") options.backlight.gamma.output = value;
        else if (key == "backlight_socket") options.backlight.gamma.socket = value;
        else if (key == "backlight_root") options.backlight.root = value;
        else if (key == "backlight_device") options.backlight.device = value;
        else if (key == "keyboard_pwm_root") options.keyboard_backlight.root = value;
        else if (key == "keyboard_pwm_device_tree_root") options.keyboard_backlight.device_tree_root = value;
        else if (key == "keyboard_pwm_of_node") options.keyboard_backlight.of_node = value;
        else if (key == "keyboard_pwm_channel")
        {
            const auto channel = integer(value);
            if (channel < 0 || channel > 255) throw std::runtime_error("Invalid keyboard PWM channel");
            options.keyboard_backlight.channel = static_cast<uint16_t>(channel);
        }
        else if (key == "keyboard_pwm_period_ns")
        {
            const auto period = integer(value);
            if (period <= 0 || period > 1000000000) throw std::runtime_error("Invalid keyboard PWM period");
            options.keyboard_backlight.initial_period_ns = static_cast<uint32_t>(period);
        }
        else if (key == "keyboard_pwm_max_duty_permille")
        {
            const auto duty = integer(value);
            if (duty <= 0 || duty > 1000) throw std::runtime_error("Invalid keyboard PWM duty limit");
            options.keyboard_backlight.max_duty_permille = static_cast<uint16_t>(duty);
        }
        else if (key == "keyboard_pwm_export")
        {
            if (value != "true" && value != "false" && value != "1" && value != "0") throw std::runtime_error("Invalid keyboard PWM export flag");
            options.keyboard_backlight.export_channel = value == "true" || value == "1";
        }
        else if (key == "screen_power") options.screen_power = value;
        else if (key == "screen_power_output") options.screen_power_output = value;
        else if (key == "gps_protocol") options.gnss.protocol = value;
        else if (key == "gps_device") options.gnss.device = value;
        else if (key == "gps_power_gpiochip") options.gnss.power.chip = value;
        else if (key == "gps_power_line") options.gnss.power.line = integer(value);
        else if (key == "gps_power_startup_ms")
        {
            const int delay = integer(value);
            if (delay < 0) throw std::runtime_error("Invalid GNSS power startup delay");
            options.gnss.power.startup_ms = static_cast<unsigned>(delay);
        }
        else if (key == "gps_baud")
        {
            const int baud = integer(value);
            if (baud < 0) throw std::runtime_error("Invalid GNSS baud");
            options.gnss.baud = static_cast<unsigned>(baud);
        }
        else if (key == "gps_modem_lines" || key == "gps_gnss_only_setup" || key == "gps_power_active_low")
        {
            if (value != "true" && value != "false") throw std::runtime_error("Invalid GNSS boolean");
            if (key == "gps_modem_lines") options.gnss.modem_lines = value == "true";
            else if (key == "gps_power_active_low") options.gnss.power.active_low = value == "true";
            else options.gnss.gnss_only_setup = value == "true";
        }
        else if (key == "radio_driver") options.radio.driver = value;
        else if (key == "radio_rf_switch_profile")
        {
            if (!platform::linux_runtime::parse_lr2021_rf_switch_profile(value, options.radio.rf_switch_profile))
                throw std::runtime_error("Unknown LR2021 RF switch profile");
        }
        else if (key == "radio_spi") options.radio.bus.spi_device = value;
        else if (key == "radio_gpiochip") options.radio.bus.gpiochip = value;
        else if (key == "radio_reset") options.radio.bus.reset_gpio = integer(value);
        else if (key == "radio_busy") options.radio.bus.busy_gpio = integer(value);
        else if (key == "radio_irq") options.radio.bus.irq_gpio = integer(value);
        else if (key == "radio_power") options.radio.bus.power_gpio = integer(value);
        else if (key == "radio_spi_hz")
        {
            const int speed = integer(value);
            if (speed < 0) throw std::runtime_error("Invalid radio SPI speed");
            options.radio.bus.spi_speed_hz = static_cast<uint32_t>(speed);
        }
        else if (key == "radio_tcxo_voltage")
        {
            std::size_t used = 0;
            options.radio.tcxo_voltage = std::stof(value, &used);
            if (used != value.size()) throw std::runtime_error("Invalid TCXO voltage");
        }
        else if (key == "imu_expected")
        {
            if (value != "true" && value != "false") throw std::runtime_error("imu_expected must be true or false");
            options.sensors.expected = value == "true";
        }
        else if (key == "imu_iio_root") options.sensors.root = value;
        else if (key == "imu_iio_device") options.sensors.device = value;
        else if (key == "imu_mount_rotation") options.sensors.mount_rotation = integer(value);
        else if (key == "orientation_policy")
        {
            if (value != "landscape_locked" && value != "sensor_landscape_only")
                throw std::runtime_error("Unsupported orientation policy for landscape UX");
            options.sensors.landscape_rotation = value == "sensor_landscape_only";
        }
        else if (key == "tmap_creation")
        {
            if (value != "true" && value != "false") throw std::runtime_error("tmap_creation must be true or false");
            options.tmap_creation = value == "true";
        }
        else if (key == "hardware_keyboard" && value == "auto") options.auto_keyboard = true;
        else if (key == "fullscreen" || key == "hardware_keyboard")
        {
            if (value != "true" && value != "false")
                throw std::runtime_error(key + " must be true or false");
            if (key == "fullscreen") options.fullscreen = value == "true";
            else
            {
                options.hardware_keyboard = value == "true";
                options.auto_keyboard = false;
            }
        }
        else throw std::runtime_error("Unknown device option: " + key);
    }
    validateDeviceOptions(options);
    return options;
}

bool hardwareKeyboardPresent()
{
#if defined(__linux__)
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator("/dev/input", error))
    {
        if (entry.path().filename().string().rfind("event", 0) != 0) continue;
        const int descriptor = open(entry.path().c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (descriptor < 0) continue;
        std::array<unsigned char, (KEY_MAX + 8) / 8> keys{};
        const bool read = ioctl(descriptor, EVIOCGBIT(EV_KEY, keys.size()), keys.data()) >= 0;
        close(descriptor);
        const auto has = [&](unsigned key)
        { return (keys[key / 8] & (1U << (key % 8))) != 0; };
        if (read && has(KEY_A) && has(KEY_ENTER) && has(KEY_SPACE)) return true;
    }
#endif
    return false;
}
} // namespace platform::desktop

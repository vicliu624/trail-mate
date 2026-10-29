#include "platform/desktop/device_options.h"
#include <cstdio>
#include <stdexcept>

int main(int argc, char** argv)
{
    if (argc != 2) return 1;
    const auto options = platform::desktop::loadDeviceOptions(argv[1]);
    if (options.width != 1232 || options.height != 568 || options.rotation != 0 || !options.auto_keyboard ||
        !options.fullscreen || options.ux_pack != "t_display_p4_touch") return 2;
    if (options.radio.driver != "lr2021" || options.radio.bus.spi_device != "/dev/spidev0.0" ||
        options.radio.bus.reset_gpio != 7 || options.radio.bus.busy_gpio != 24 ||
        options.radio.bus.irq_gpio != -1 || options.radio.tcxo_voltage != 3.0f) return 4;
    if (options.sensors.expected || options.sensors.landscape_rotation) return 5;
    if (options.runtime_mode != platform::linux_runtime::LinuxRuntimeMode::DeviceRealMesh) return 7;
    if (options.gnss.protocol != "nmea" || !options.gnss.device.empty() ||
        options.gnss.baud != 0 || options.gnss.modem_lines || options.gnss.gnss_only_setup) return 6;
    if (options.gnss.power.line != -1) return 8;
    if (options.screen_power != "none" || !options.screen_power_output.empty()) return 10;
    auto display = options;
    display.screen_power = "wayland";
    display.screen_power_output = "external-display";
    platform::desktop::validateDeviceOptions(display);
    display.screen_power = "invalid";
    try
    {
        platform::desktop::validateDeviceOptions(display);
        return 11;
    }
    catch (const std::runtime_error&)
    {
    }
    auto conflict = options;
    conflict.gnss.power.chip = "/dev/gpiochip0";
    conflict.gnss.power.line = options.radio.bus.reset_gpio;
    bool rejected = false;
    try
    {
        platform::desktop::validateDeviceOptions(conflict);
    }
    catch (const std::runtime_error&)
    {
        rejected = true;
    }
    if (!rejected) return 9;
    // A second device can use this same backend without acquiring CM0 defaults.
    auto other = options;
    other.width = 800;
    other.height = 480;
    other.rotation = 0;
    other.fullscreen = false;
    other.gnss.power = {};
    platform::desktop::validateDeviceOptions(other);
    other.rotation = 45;
    try
    {
        platform::desktop::validateDeviceOptions(other);
    }
    catch (const std::runtime_error&)
    {
        return 0;
    }
    std::fputs("Invalid rotation was accepted\n", stderr);
    return 3;
}

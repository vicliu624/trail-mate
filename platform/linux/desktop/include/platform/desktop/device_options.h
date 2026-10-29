#pragma once

#include "platform/linux/backlight.h"
#include "platform/linux/gnss_source.h"
#include "platform/linux/iio_sensors.h"
#include "platform/linux/keyboard_backlight.h"
#include "platform/linux/packet_radio.h"
#include "platform/linux/runtime_mode.h"
#include "platform/linux/runtime_paths.h"
#include <string>

namespace platform::desktop
{
struct DeviceOptions
{
    std::string title = "Trail Mate";
    std::string ux_pack;
    int width = 0;
    int height = 0;
    int rotation = 0;
    bool fullscreen = true;
    bool hardware_keyboard = false;
    bool auto_keyboard = false;
    bool tmap_creation = false;
    platform::linux_runtime::PacketRadioHardware radio{};
    platform::linux_runtime::IioSensorOptions sensors{};
    platform::linux_runtime::RuntimePaths paths{};
    platform::linux_runtime::GnssSourceOptions gnss{};
    platform::linux_runtime::BacklightOptions backlight{};
    platform::linux_runtime::KeyboardBacklightOptions keyboard_backlight{};
    std::string screen_power = "none";
    std::string screen_power_output;
    platform::linux_runtime::LinuxRuntimeMode runtime_mode = platform::linux_runtime::LinuxRuntimeMode::DeviceLocal;
};

// Configuration belongs to the selected device, never to the window backend.
DeviceOptions loadDeviceOptions(const std::string& path);
void validateDeviceOptions(const DeviceOptions& options);
bool hardwareKeyboardPresent();
} // namespace platform::desktop

#include "platform/ui/device_runtime.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>

#include "platform/linux/application_lifecycle.h"
#include "platform/linux/backlight.h"
#include "platform/linux/battery_monitor.h"
#include "platform/linux/build_identity.h"
#include "platform/linux/env_config.h"
#include "platform/linux/gnss_source.h"
#include "platform/linux/keyboard_backlight.h"
#include "platform/linux/memory_monitor.h"
#include "platform/linux/native_audio_service.h"
#include "platform/linux/rtc_clock.h"
#include "platform/linux/runtime_paths.h"
#include "platform/linux/storage_monitor.h"
#if defined(TRAIL_MATE_LINUX_HAS_MEDIA)
#include "platform/audio/media_volume.h"
#endif

namespace platform::ui::device
{
namespace
{

using namespace ::platform::linux_runtime;

constexpr const char* kBatteryLevelEnv = "TRAIL_MATE_BATTERY_LEVEL";
constexpr const char* kBatteryChargingEnv = "TRAIL_MATE_BATTERY_CHARGING";
constexpr const char* kGpsSupportedEnv = "TRAIL_MATE_GPS_SUPPORTED";
constexpr const char* kGpsReadyEnv = "TRAIL_MATE_GPS_READY";
constexpr const char* kPowerTierEnv = "TRAIL_MATE_POWER_TIER";

std::atomic<uint8_t> s_message_tone_volume{45};

} // namespace

void delay_ms(uint32_t ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void restart()
{
    if (::platform::linux_runtime::request_application_restart()) return;
    std::fflush(nullptr);
    std::exit(0);
}

bool rtc_ready()
{
    return ::platform::linux_runtime::linux_rtc_snapshot().valid;
}

BatteryInfo battery_info()
{
    BatteryInfo info{};
    const bool level_env_set = std::getenv(kBatteryLevelEnv) != nullptr;
    if (level_env_set)
    {
        const int level = platform::linux_runtime::env_int(kBatteryLevelEnv, -1);
        info.available = level >= 0;
        info.level = info.available ? std::clamp(level, 0, 100) : -1;
        info.charging = platform::linux_runtime::env_flag(kBatteryChargingEnv);
        info.external_power = info.charging;
        return info;
    }

    return ::platform::linux_runtime::linux_battery_snapshot();
}

MemoryStats memory_stats()
{
    return ::platform::linux_runtime::linux_memory_snapshot();
}

const char* firmware_version()
{
    return ::platform::linux_runtime::runtime_firmware_version();
}

void handle_low_battery(const BatteryInfo&)
{
}

bool supports_screen_brightness()
{
    return ::platform::linux_runtime::backlight_snapshot().available;
}

bool supports_keyboard_backlight()
{
    return ::platform::linux_runtime::keyboard_backlight_snapshot().available;
}

bool supports_configurable_battery_gauge()
{
    return false;
}

void reload_configurable_battery_gauge()
{
}

uint8_t screen_brightness()
{
    return ::platform::linux_runtime::backlight_snapshot().level;
}

uint8_t screen_brightness_max()
{
    return supports_screen_brightness() ? 255 : 0;
}

void set_screen_brightness(uint8_t level)
{
    (void)::platform::linux_runtime::request_backlight(level);
}

uint8_t keyboard_backlight()
{
    return ::platform::linux_runtime::keyboard_backlight_snapshot().level;
}

uint8_t keyboard_backlight_max()
{
    return supports_keyboard_backlight() ? 255 : 0;
}

void set_keyboard_backlight(uint8_t level)
{
    (void)::platform::linux_runtime::request_keyboard_backlight(level);
}

void trigger_haptic()
{
}

uint8_t default_message_tone_volume()
{
#if defined(TRAIL_MATE_LINUX_HAS_MEDIA)
    return ::platform::audio::media_volume(s_message_tone_volume.load());
#else
    return s_message_tone_volume.load();
#endif
}

void set_message_tone_volume(uint8_t volume_percent)
{
    s_message_tone_volume = std::min<unsigned>(volume_percent, 100);
#if defined(TRAIL_MATE_LINUX_HAS_MEDIA)
    ::platform::audio::publish_media_volume(s_message_tone_volume.load());
#endif
}

void play_message_tone()
{
    (void)::platform::linux_runtime::queue_message_tone(default_message_tone_volume());
}

bool sd_ready()
{
    return ::platform::linux_runtime::linux_storage_snapshot().available;
}

bool card_ready()
{
    return sd_ready();
}

bool gps_ready()
{
    const auto options = platform::linux_runtime::gnss_source_options();
    const bool ready = options.protocol != "nordic_slm" || platform::linux_runtime::gnss_source_ready();
    return platform::linux_runtime::env_flag(kGpsReadyEnv, ready);
}

bool gps_supported()
{
    return platform::linux_runtime::env_flag(kGpsSupportedEnv, true);
}

int power_tier()
{
    return platform::linux_runtime::env_int(kPowerTierEnv, 0);
}

} // namespace platform::ui::device

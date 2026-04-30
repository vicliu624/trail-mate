#include "platform/ui/device_runtime.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace platform::ui::device
{
namespace
{

constexpr const char* kFirmwareVersionEnv = "TRAIL_MATE_FIRMWARE_VERSION";
constexpr const char* kBatteryLevelEnv = "TRAIL_MATE_BATTERY_LEVEL";
constexpr const char* kBatteryChargingEnv = "TRAIL_MATE_BATTERY_CHARGING";
constexpr const char* kSdRootEnv = "TRAIL_MATE_SD_ROOT";
constexpr const char* kSettingsRootEnv = "TRAIL_MATE_SETTINGS_ROOT";
constexpr const char* kGpsSupportedEnv = "TRAIL_MATE_GPS_SUPPORTED";
constexpr const char* kGpsReadyEnv = "TRAIL_MATE_GPS_READY";
constexpr const char* kPowerTierEnv = "TRAIL_MATE_POWER_TIER";

uint8_t s_screen_brightness = 0;
uint8_t s_message_tone_volume = 45;

bool env_flag_enabled(const char* name)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0')
    {
        return false;
    }
    return std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
           std::strcmp(value, "TRUE") == 0 || std::strcmp(value, "yes") == 0 ||
           std::strcmp(value, "YES") == 0;
}

bool env_flag_or_default(const char* name, bool fallback)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0')
    {
        return fallback;
    }
    return env_flag_enabled(name);
}

int env_int_or_default(const char* name, int fallback)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0')
    {
        return fallback;
    }

    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || (end && *end != '\0'))
    {
        return fallback;
    }
    return static_cast<int>(parsed);
}

bool path_exists_from_env(const char* name)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0')
    {
        return false;
    }
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::path(value), ec);
}

std::filesystem::path default_storage_root()
{
    if (const char* configured = std::getenv(kSdRootEnv))
    {
        if (configured[0] != '\0')
        {
            return std::filesystem::path(configured);
        }
    }

    if (const char* configured = std::getenv(kSettingsRootEnv))
    {
        if (configured[0] != '\0')
        {
            return std::filesystem::path(configured) / "sdcard";
        }
    }

#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"))
    {
        if (appdata[0] != '\0')
        {
            return std::filesystem::path(appdata) / "TrailMateCardputerZero" / "sdcard";
        }
    }
#endif

    if (const char* home = std::getenv("HOME"))
    {
        if (home[0] != '\0')
        {
            return std::filesystem::path(home) / ".trailmate_cardputer_zero" / "sdcard";
        }
    }

    return std::filesystem::current_path() / ".trailmate_cardputer_zero" / "sdcard";
}

bool ensure_storage_root()
{
    std::error_code ec;
    std::filesystem::create_directories(default_storage_root(), ec);
    return !ec;
}

#if defined(__linux__)
bool read_meminfo_kb(const char* label, std::size_t* out_kb)
{
    if (!label || !out_kb)
    {
        return false;
    }

    std::ifstream stream("/proc/meminfo");
    if (!stream.is_open())
    {
        return false;
    }

    std::string key;
    std::size_t value_kb = 0;
    std::string unit;
    while (stream >> key >> value_kb >> unit)
    {
        if (!key.empty() && key.back() == ':')
        {
            key.pop_back();
        }
        if (key == label)
        {
            *out_kb = value_kb;
            return true;
        }
    }
    return false;
}
#endif

} // namespace

void delay_ms(uint32_t ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void restart()
{
    std::fflush(nullptr);
    std::exit(0);
}

bool rtc_ready()
{
    return true;
}

BatteryInfo battery_info()
{
    BatteryInfo info{};
    const int level = env_int_or_default(kBatteryLevelEnv, -1);
    info.available = level >= 0;
    info.level = level;
    info.charging = env_flag_enabled(kBatteryChargingEnv);
    return info;
}

MemoryStats memory_stats()
{
    MemoryStats stats{};

#if defined(__linux__)
    std::size_t mem_total_kb = 0;
    std::size_t mem_available_kb = 0;
    if (read_meminfo_kb("MemTotal", &mem_total_kb))
    {
        stats.ram_total_bytes = mem_total_kb * 1024U;
    }
    if (read_meminfo_kb("MemAvailable", &mem_available_kb))
    {
        stats.ram_free_bytes = mem_available_kb * 1024U;
    }
#endif

    stats.psram_total_bytes = 0;
    stats.psram_free_bytes = 0;
    stats.psram_available = false;
    return stats;
}

const char* firmware_version()
{
    const char* configured = std::getenv(kFirmwareVersionEnv);
    return (configured && configured[0] != '\0') ? configured : "linux-dev";
}

void handle_low_battery(const BatteryInfo&)
{
}

bool supports_screen_brightness()
{
    return false;
}

uint8_t screen_brightness()
{
    return s_screen_brightness;
}

void set_screen_brightness(uint8_t level)
{
    s_screen_brightness = level;
}

void trigger_haptic()
{
}

uint8_t default_message_tone_volume()
{
    return s_message_tone_volume;
}

void set_message_tone_volume(uint8_t volume_percent)
{
    s_message_tone_volume = volume_percent;
}

void play_message_tone()
{
}

bool sd_ready()
{
    return path_exists_from_env(kSdRootEnv) || ensure_storage_root();
}

bool card_ready()
{
    return sd_ready();
}

bool gps_ready()
{
    return env_flag_or_default(kGpsReadyEnv, true);
}

bool gps_supported()
{
    return env_flag_or_default(kGpsSupportedEnv, true);
}

int power_tier()
{
    return env_int_or_default(kPowerTierEnv, 0);
}

} // namespace platform::ui::device

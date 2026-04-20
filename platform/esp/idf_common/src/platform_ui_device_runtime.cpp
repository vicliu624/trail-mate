#include "platform/ui/device_runtime.h"

#include <ctime>

#include "boards/t_display_p4/board_profile.h"
#include "boards/tab5/rtc_runtime.h"
#include "boards/tab5/tab5_board.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform/esp/idf_common/bsp_runtime.h"
#include "platform/esp/idf_common/gps_runtime.h"

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
#include "bsp/m5stack_tab5.h"
#endif

namespace platform::ui::device
{

namespace
{

uint8_t s_brightness_level = DEVICE_MAX_BRIGHTNESS_LEVEL;

} // namespace

void delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void restart()
{
    esp_restart();
}

bool rtc_ready()
{
    return ::boards::tab5::rtc_runtime::is_valid_epoch(std::time(nullptr));
}

BatteryInfo battery_info()
{
    BatteryInfo info{};
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    info.charging = bsp_usb_c_detect();
#endif
    return info;
}

MemoryStats memory_stats()
{
    MemoryStats stats{};
    stats.ram_total_bytes = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    stats.ram_free_bytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    stats.psram_total_bytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    stats.psram_free_bytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    stats.psram_available = stats.psram_total_bytes > 0;
    return stats;
}

const char* firmware_version()
{
    const esp_app_desc_t* desc = esp_ota_get_app_description();
    return (desc && desc->version[0] != '\0') ? desc->version : "unknown";
}

void handle_low_battery(const BatteryInfo& info)
{
    (void)info;
}

bool supports_screen_brightness()
{
    return true;
}

uint8_t screen_brightness()
{
    return s_brightness_level;
}

void set_screen_brightness(uint8_t level)
{
    s_brightness_level = level;
    const int percent = (DEVICE_MAX_BRIGHTNESS_LEVEL <= 0)
                            ? 100
                            : static_cast<int>((static_cast<uint32_t>(level) * 100U) /
                                               static_cast<uint32_t>(DEVICE_MAX_BRIGHTNESS_LEVEL));
    (void)platform::esp::idf_common::bsp_runtime::set_display_brightness(percent);
}

void trigger_haptic() {}

uint8_t default_message_tone_volume()
{
    return 45;
}

void set_message_tone_volume(uint8_t volume_percent)
{
    (void)volume_percent;
}

void play_message_tone() {}

bool sd_ready()
{
    return platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready();
}

bool card_ready()
{
    return sd_ready();
}

bool gps_ready()
{
    return platform::esp::idf_common::gps_runtime::is_enabled() ||
           platform::esp::idf_common::gps_runtime::is_powered();
}

bool gps_supported()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return ::boards::tab5::Tab5Board::hasGpsUart();
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return ::boards::t_display_p4::kBoardProfile.has_gps_uart;
#else
    return false;
#endif
}

int power_tier()
{
    const BatteryInfo info = battery_info();
    if (!info.available || info.level < 0)
    {
        return 0;
    }
    return info.level <= 15 ? 1 : 0;
}

} // namespace platform::ui::device

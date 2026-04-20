#include "platform/ui/device_runtime.h"

#include "board/BoardBase.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform/esp/arduino_common/battery_guard.h"

namespace platform::ui::device
{

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
    return board.isRTCReady();
}

BatteryInfo battery_info()
{
    BatteryInfo info{};
    info.available = true;
    info.charging = board.isCharging();
    info.level = board.getBatteryLevel();
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
    if (!info.available || info.level < 0)
    {
        return;
    }
    platform::esp::arduino_common::handleLowBattery(info.level, info.charging);
}

bool supports_screen_brightness()
{
    return true;
}

uint8_t screen_brightness()
{
    return board.getBrightness();
}

void set_screen_brightness(uint8_t level)
{
    board.setBrightness(level);
}

void trigger_haptic()
{
    board.vibrator();
}

uint8_t default_message_tone_volume()
{
    return board.getMessageToneVolume();
}

void set_message_tone_volume(uint8_t volume_percent)
{
    board.setMessageToneVolume(volume_percent);
}

void play_message_tone()
{
    board.playMessageTone();
}

bool sd_ready()
{
    return board.isSDReady();
}

bool card_ready()
{
    return board.isCardReady();
}

bool gps_ready()
{
    return board.isGPSReady();
}

bool gps_supported()
{
    return board.hasGPSHardware();
}

int power_tier()
{
    return board.getPowerTier();
}

} // namespace platform::ui::device

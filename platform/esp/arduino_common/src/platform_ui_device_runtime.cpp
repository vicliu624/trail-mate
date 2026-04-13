#include "platform/ui/device_runtime.h"

#include "board/BoardBase.h"
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

int power_tier()
{
    return board.getPowerTier();
}

} // namespace platform::ui::device

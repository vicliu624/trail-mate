#include "platform/ui/device_runtime.h"

#include <ctime>

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform/esp/idf_common/bsp_runtime.h"

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
#include "bsp/m5stack_tab5.h"
#endif

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
    return std::time(nullptr) > 0;
}

BatteryInfo battery_info()
{
    BatteryInfo info{};
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    info.charging = bsp_usb_c_detect();
#endif
    return info;
}

void handle_low_battery(const BatteryInfo& info)
{
    (void)info;
}

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
    return platform::esp::idf_common::bsp_runtime::gps_capable();
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

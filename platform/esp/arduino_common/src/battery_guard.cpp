#include "platform/esp/arduino_common/battery_guard.h"

#include "board/BoardBase.h"
#include "lvgl.h"
#include "ui/widgets/system_notification.h"

namespace
{

#if defined(ARDUINO_LILYGO_LORA_SX1262)
constexpr int kLowBatteryWarnPercent = 15;
constexpr int kCriticalBatteryPercent = 5;
constexpr uint8_t kCriticalStreakToShutdown = 2;
constexpr uint32_t kLowBatteryWarnCooldownMs = 15 * 60 * 1000;
constexpr uint32_t kCriticalBatteryWarnCooldownMs = 5 * 60 * 1000;
constexpr uint32_t kCriticalShutdownDelayMs = 3000;

uint32_t s_last_low_battery_warn_ms = 0;
uint32_t s_last_critical_battery_warn_ms = 0;
uint8_t s_critical_battery_streak = 0;
lv_timer_t* s_low_battery_shutdown_timer = nullptr;

void cancelLowBatteryShutdown()
{
    if (s_low_battery_shutdown_timer)
    {
        lv_timer_del(s_low_battery_shutdown_timer);
        s_low_battery_shutdown_timer = nullptr;
    }
}

void lowBatteryShutdownCallback(lv_timer_t* timer)
{
    (void)timer;
    s_low_battery_shutdown_timer = nullptr;
    board.softwareShutdown();
}
#endif

} // namespace

namespace platform::esp::arduino_common
{

void handleLowBattery(int level, bool charging)
{
#if defined(ARDUINO_LILYGO_LORA_SX1262)
    if (level < 0)
    {
        return;
    }

    if (charging)
    {
        board.setPowerTier(0);
        s_critical_battery_streak = 0;
        cancelLowBatteryShutdown();
        return;
    }

    if (level <= 10)
    {
        board.setPowerTier(2);
    }
    else if (level <= 20)
    {
        board.setPowerTier(1);
    }
    else
    {
        board.setPowerTier(0);
    }

    const uint32_t now_ms = millis();

    if (level <= kCriticalBatteryPercent)
    {
        s_critical_battery_streak++;
        if (now_ms - s_last_critical_battery_warn_ms >= kCriticalBatteryWarnCooldownMs)
        {
            ui::SystemNotification::show("Battery critical", 2500);
            s_last_critical_battery_warn_ms = now_ms;
        }
        if (s_critical_battery_streak >= kCriticalStreakToShutdown && s_low_battery_shutdown_timer == nullptr)
        {
            ui::SystemNotification::show("Shutting down to protect battery", 3000);
            s_low_battery_shutdown_timer = lv_timer_create(lowBatteryShutdownCallback, kCriticalShutdownDelayMs, nullptr);
            if (s_low_battery_shutdown_timer)
            {
                lv_timer_set_repeat_count(s_low_battery_shutdown_timer, 1);
            }
        }
        return;
    }

    s_critical_battery_streak = 0;
    cancelLowBatteryShutdown();

    if (level <= kLowBatteryWarnPercent && now_ms - s_last_low_battery_warn_ms >= kLowBatteryWarnCooldownMs)
    {
        char msg[48];
        snprintf(msg, sizeof(msg), "Low battery: %d%%", level);
        ui::SystemNotification::show(msg, 2500);
        s_last_low_battery_warn_ms = now_ms;
    }
#else
    (void)level;
    (void)charging;
#endif
}

} // namespace platform::esp::arduino_common

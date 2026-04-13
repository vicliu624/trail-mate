/**
 * @file screen_sleep.cpp
 * @brief ESP-IDF screen sleep runtime backed by shared settings and BSP display controls.
 */

#include "screen_sleep.h"

#include <cstdint>

#include "board/BoardBase.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "platform/esp/idf_common/bsp_runtime.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/settings_store.h"

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
extern "C"
{
#include "bsp/trail_mate_tab5_runtime.h"
}
#endif

namespace
{

constexpr const char* kTag = "idf-screen-sleep";
constexpr const char* kSettingsNs = "settings";
constexpr const char* kScreenTimeoutKey = "screen_timeout";
constexpr uint32_t kScreenTimeoutMinMs = 10000;
constexpr uint32_t kScreenTimeoutMaxMs = 300000;
constexpr uint32_t kScreenTimeoutDefaultMs = 60000;
constexpr uint32_t kScreenTimeoutMaxBleSecs = 900;
constexpr uint32_t kTaskPeriodMs = 250;

ScreenSleepHooks s_hooks{};
SemaphoreHandle_t s_mutex = nullptr;
TaskHandle_t s_task = nullptr;
uint32_t s_timeout_ms = kScreenTimeoutDefaultMs;
bool s_timeout_loaded = false;
uint32_t s_last_user_activity_ms = 0;
bool s_screen_sleeping = false;
bool s_screen_sleep_disabled = false;
bool s_screen_saver_active = false;
uint8_t s_saved_screen_brightness = DEVICE_MAX_BRIGHTNESS_LEVEL;

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

uint32_t clamp_timeout_internal(uint32_t timeout_ms)
{
    if (timeout_ms < kScreenTimeoutMinMs || timeout_ms > kScreenTimeoutMaxMs)
    {
        return kScreenTimeoutDefaultMs;
    }
    return timeout_ms;
}

void ensure_mutex()
{
    if (s_mutex == nullptr)
    {
        s_mutex = xSemaphoreCreateMutex();
    }
}

void load_timeout_if_needed_locked()
{
    if (s_timeout_loaded)
    {
        return;
    }
    s_timeout_ms = clamp_timeout_internal(
        platform::ui::settings_store::get_uint(kSettingsNs, kScreenTimeoutKey, kScreenTimeoutDefaultMs));
    s_timeout_loaded = true;
}

void wake_display_locked()
{
    platform::ui::device::set_screen_brightness(s_saved_screen_brightness);
    s_last_user_activity_ms = now_ms();
    s_screen_sleeping = false;
    s_screen_saver_active = false;
    ESP_LOGI(kTag, "Display wake");
}

void sleep_display_locked()
{
    s_saved_screen_brightness = platform::ui::device::screen_brightness();
    platform::esp::idf_common::bsp_runtime::sleep_display();
    s_screen_sleeping = true;
    s_screen_saver_active = false;
    ESP_LOGI(kTag, "Display sleep");
}

void notify_wake()
{
    if (s_hooks.on_wake_from_sleep)
    {
        s_hooks.on_wake_from_sleep();
    }
}

bool wake_requested_by_touch_irq_locked()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return s_screen_sleeping && trail_mate_tab5_touch_interrupt_active();
#else
    return false;
#endif
}

void screen_sleep_task(void*)
{
    while (true)
    {
        bool should_notify_wake = false;
        ensure_mutex();
        if (s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE)
        {
            load_timeout_if_needed_locked();
            const uint32_t elapsed = now_ms() - s_last_user_activity_ms;
            if (s_screen_sleep_disabled)
            {
                if (s_screen_sleeping)
                {
                    wake_display_locked();
                    should_notify_wake = true;
                }
            }
            else if (wake_requested_by_touch_irq_locked())
            {
                ESP_LOGI(kTag, "Display wake requested by Tab5 touch interrupt");
                wake_display_locked();
                should_notify_wake = true;
            }
            else if ((s_screen_sleeping == false) && elapsed >= s_timeout_ms)
            {
                sleep_display_locked();
            }
            xSemaphoreGive(s_mutex);
        }
        if (should_notify_wake)
        {
            notify_wake();
        }
        vTaskDelay(pdMS_TO_TICKS(kTaskPeriodMs));
    }
}

} // namespace

uint32_t clampScreenTimeoutMs(uint32_t timeout_ms)
{
    return clamp_timeout_internal(timeout_ms);
}
uint32_t getScreenSleepTimeout()
{
    ensure_mutex();
    if (s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE)
    {
        load_timeout_if_needed_locked();
        const uint32_t timeout_ms = s_timeout_ms;
        xSemaphoreGive(s_mutex);
        return timeout_ms;
    }
    return kScreenTimeoutDefaultMs;
}

uint16_t readScreenTimeoutSecs()
{
    const uint32_t timeout_secs = getScreenSleepTimeout() / 1000U;
    return static_cast<uint16_t>(timeout_secs > kScreenTimeoutMaxBleSecs ? kScreenTimeoutMaxBleSecs : timeout_secs);
}

void setScreenSleepTimeout(uint32_t timeout_ms)
{
    const uint32_t clamped = clamp_timeout_internal(timeout_ms);
    ensure_mutex();
    if (s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE)
    {
        s_timeout_ms = clamped;
        s_timeout_loaded = true;
        xSemaphoreGive(s_mutex);
    }
    platform::ui::settings_store::put_uint(kSettingsNs, kScreenTimeoutKey, clamped);
}

void initScreenSleepRuntime(const ScreenSleepHooks& hooks)
{
    platform::esp::idf_common::bsp_runtime::ensure_nvs_ready();
    s_hooks = hooks;
    ensure_mutex();

    if (s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE)
    {
        load_timeout_if_needed_locked();
        s_last_user_activity_ms = now_ms();
        xSemaphoreGive(s_mutex);
    }

    if (s_task == nullptr)
    {
        const BaseType_t rc = xTaskCreate(screen_sleep_task,
                                          "screen_sleep",
                                          4096,
                                          nullptr,
                                          2,
                                          &s_task);
        if (pdPASS == rc)
        {
            return;
        }
        ESP_LOGE(kTag, "Failed to start screen sleep task rc=%ld", static_cast<long>(rc));
        s_task = nullptr;
    }
}

bool isScreenSleeping()
{
    ensure_mutex();
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        const bool sleeping = s_screen_sleeping;
        xSemaphoreGive(s_mutex);
        return sleeping;
    }
    return false;
}

bool isScreenSleepDisabled()
{
    ensure_mutex();
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        const bool disabled = s_screen_sleep_disabled;
        xSemaphoreGive(s_mutex);
        return disabled;
    }
    return false;
}

bool isScreenSaverActive()
{
    ensure_mutex();
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        const bool active = s_screen_saver_active;
        xSemaphoreGive(s_mutex);
        return active;
    }
    return false;
}
void wakeScreenSaver()
{
    updateUserActivity();
}

void enterFromScreenSaver()
{
    updateUserActivity();
    if (s_hooks.show_main_menu)
    {
        s_hooks.show_main_menu();
    }
}

void updateUserActivity()
{
    bool woke_from_sleep = false;
    ensure_mutex();
    if (s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE)
    {
        s_last_user_activity_ms = now_ms();
        if (s_screen_sleeping || s_screen_saver_active)
        {
            wake_display_locked();
            woke_from_sleep = true;
        }
        xSemaphoreGive(s_mutex);
    }
    if (woke_from_sleep)
    {
        notify_wake();
    }
}

void disableScreenSleep()
{
    bool woke = false;
    ensure_mutex();
    if (s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE)
    {
        s_screen_sleep_disabled = true;
        s_last_user_activity_ms = now_ms();
        if (s_screen_sleeping)
        {
            wake_display_locked();
            woke = true;
        }
        xSemaphoreGive(s_mutex);
    }
    if (woke)
    {
        notify_wake();
    }
}

void enableScreenSleep()
{
    ensure_mutex();
    if (s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE)
    {
        s_screen_sleep_disabled = false;
        s_last_user_activity_ms = now_ms();
        xSemaphoreGive(s_mutex);
    }
}

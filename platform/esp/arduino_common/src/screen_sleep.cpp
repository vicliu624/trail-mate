/**
 * @file screen_sleep.cpp
 * @brief Shared screen sleep timeout/runtime helpers.
 */

#include "screen_sleep.h"

#include <Preferences.h>
#include <cstdio>

#include "board/BoardBase.h"
#include "display/DisplayConfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

namespace
{
constexpr const char* kSettingsNs = "settings";
constexpr const char* kScreenTimeoutKey = "screen_timeout";
constexpr uint32_t kScreenTimeoutMinMs = 10000;
constexpr uint32_t kScreenTimeoutMaxMs = 300000;
constexpr uint32_t kScreenTimeoutDefaultMs = 60000;
constexpr uint32_t kScreenTimeoutMaxBleSecs = 900;
constexpr uint32_t kScreenSaverDurationMs = 3000;

ScreenSleepHooks s_hooks{};

portMUX_TYPE s_screen_timeout_mux = portMUX_INITIALIZER_UNLOCKED;
uint32_t s_screen_sleep_timeout_ms = kScreenTimeoutDefaultMs;
bool s_screen_sleep_timeout_loaded = false;

SemaphoreHandle_t s_activity_mutex = nullptr;
TaskHandle_t s_screen_sleep_task_handle = nullptr;
uint32_t s_last_user_activity_time = 0;
bool s_screen_sleeping = false;
bool s_screen_sleep_disabled = false;
uint8_t s_saved_keyboard_brightness = 127;
bool s_screen_saver_active = false;
lv_obj_t* s_screen_saver_layer = nullptr;
lv_obj_t* s_screen_saver_time_label = nullptr;
lv_obj_t* s_screen_saver_unread_label = nullptr;
lv_obj_t* s_screen_saver_hint_label = nullptr;
lv_timer_t* s_screen_saver_timer = nullptr;

uint32_t readPersistedScreenTimeoutMs()
{
    Preferences prefs;
    prefs.begin(kSettingsNs, true);
    const uint32_t value = prefs.getUInt(kScreenTimeoutKey, 0);
    prefs.end();
    return clampScreenTimeoutMs(value);
}

void writePersistedScreenTimeoutMs(uint32_t timeout_ms)
{
    Preferences prefs;
    prefs.begin(kSettingsNs, false);
    prefs.putUInt(kScreenTimeoutKey, timeout_ms);
    prefs.end();
}

void cacheScreenTimeoutMs(uint32_t timeout_ms)
{
    taskENTER_CRITICAL(&s_screen_timeout_mux);
    s_screen_sleep_timeout_ms = timeout_ms;
    s_screen_sleep_timeout_loaded = true;
    taskEXIT_CRITICAL(&s_screen_timeout_mux);
}

bool isScreenTimeoutLoaded()
{
    taskENTER_CRITICAL(&s_screen_timeout_mux);
    const bool loaded = s_screen_sleep_timeout_loaded;
    taskEXIT_CRITICAL(&s_screen_timeout_mux);
    return loaded;
}

uint32_t cachedScreenTimeoutMs()
{
    taskENTER_CRITICAL(&s_screen_timeout_mux);
    const uint32_t timeout_ms = s_screen_sleep_timeout_ms;
    taskEXIT_CRITICAL(&s_screen_timeout_mux);
    return timeout_ms;
}

bool formatScreenSaverTime(char* out, size_t out_len)
{
    if (s_hooks.format_time)
    {
        return s_hooks.format_time(out, out_len);
    }
    return false;
}

int readUnreadCount()
{
    if (s_hooks.read_unread_count)
    {
        return s_hooks.read_unread_count();
    }
    return 0;
}

void showMainMenu()
{
    if (s_hooks.show_main_menu)
    {
        s_hooks.show_main_menu();
    }
}

void notifyWakeFromSleep()
{
    if (s_hooks.on_wake_from_sleep)
    {
        s_hooks.on_wake_from_sleep();
    }
}

void hide_screen_saver_layer()
{
    if (!s_screen_saver_layer)
    {
        return;
    }
    lv_obj_add_flag(s_screen_saver_layer, LV_OBJ_FLAG_HIDDEN);
    if (s_screen_saver_timer)
    {
        lv_timer_pause(s_screen_saver_timer);
    }
}

void screen_saver_refresh()
{
    if (!s_screen_saver_layer || !s_screen_saver_time_label || !s_screen_saver_unread_label)
    {
        return;
    }

    char time_buf[16] = "--:--";
    if (!formatScreenSaverTime(time_buf, sizeof(time_buf)))
    {
        snprintf(time_buf, sizeof(time_buf), "--:--");
    }
    lv_label_set_text(s_screen_saver_time_label, time_buf);

    const int unread = readUnreadCount();
    char unread_buf[32];
    if (unread > 0)
    {
        snprintf(unread_buf, sizeof(unread_buf), "Unread: %d", unread);
    }
    else
    {
        snprintf(unread_buf, sizeof(unread_buf), "Unread: 0");
    }
    lv_label_set_text(s_screen_saver_unread_label, unread_buf);
}

void screen_saver_timer_cb(lv_timer_t* /*timer*/)
{
    if (s_activity_mutex != nullptr)
    {
        if (xSemaphoreTake(s_activity_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            s_screen_saver_active = false;
            s_screen_sleeping = true;
            xSemaphoreGive(s_activity_mutex);
        }
    }
    hide_screen_saver_layer();
    if (board.hasKeyboard())
    {
        board.keyboardSetBrightness(0);
    }
    board.setBrightness(0);
    board.enterScreenSleep();
}

void init_screen_saver()
{
    if (s_screen_saver_layer)
    {
        return;
    }

    s_screen_saver_layer = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_screen_saver_layer, LV_PCT(100), LV_PCT(100));
    lv_obj_align(s_screen_saver_layer, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_screen_saver_layer, lv_color_hex(0xF6E6C6), 0);
    lv_obj_set_style_bg_opa(s_screen_saver_layer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_screen_saver_layer, 0, 0);
    lv_obj_clear_flag(s_screen_saver_layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_screen_saver_layer, LV_OBJ_FLAG_CLICKABLE);

    s_screen_saver_time_label = lv_label_create(s_screen_saver_layer);
    lv_obj_set_style_text_color(s_screen_saver_time_label, lv_color_hex(0x6B4A1E), 0);
    lv_obj_set_style_text_font(s_screen_saver_time_label, &lv_font_montserrat_36, 0);
    lv_label_set_text(s_screen_saver_time_label, "--:--");
    lv_obj_align(s_screen_saver_time_label, LV_ALIGN_CENTER, 0, -26);

    s_screen_saver_unread_label = lv_label_create(s_screen_saver_layer);
    lv_obj_set_style_text_color(s_screen_saver_unread_label, lv_color_hex(0x6B4A1E), 0);
    lv_obj_set_style_text_font(s_screen_saver_unread_label, &lv_font_montserrat_20, 0);
    lv_label_set_text(s_screen_saver_unread_label, "Unread: 0");
    lv_obj_align(s_screen_saver_unread_label, LV_ALIGN_CENTER, 0, 10);

    s_screen_saver_hint_label = lv_label_create(s_screen_saver_layer);
    lv_obj_set_style_text_color(s_screen_saver_hint_label, lv_color_hex(0x8A6A3A), 0);
    lv_obj_set_style_text_font(s_screen_saver_hint_label, &lv_font_montserrat_14, 0);
#if defined(ARDUINO_T_DECK)
    lv_label_set_text(s_screen_saver_hint_label, "Press SPACE to enter main menu");
#else
    lv_label_set_text(s_screen_saver_hint_label, "Press SPACE to enter");
#endif
    lv_obj_align(s_screen_saver_hint_label, LV_ALIGN_CENTER, 0, 40);

    lv_obj_add_flag(s_screen_saver_layer, LV_OBJ_FLAG_HIDDEN);
}

void screenSleepTask(void* pvParameters)
{
    (void)pvParameters;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t check_interval = pdMS_TO_TICKS(1000);

    while (true)
    {
        if (s_activity_mutex != nullptr)
        {
            if (xSemaphoreTake(s_activity_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
            {
                const uint32_t current_time = millis();
                const uint32_t time_since_activity = current_time - s_last_user_activity_time;
                const uint32_t current_timeout = getScreenSleepTimeout();

                if (s_screen_sleep_disabled)
                {
                    if (s_screen_sleeping)
                    {
                        s_screen_sleeping = false;
                        board.exitScreenSleep();
                        board.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
                        if (board.hasKeyboard())
                        {
                            board.keyboardSetBrightness(s_saved_keyboard_brightness);
                        }
                    }
                    xSemaphoreGive(s_activity_mutex);
                }
                else
                {
                    if (!s_screen_sleeping && time_since_activity >= current_timeout)
                    {
                        s_screen_sleeping = true;
                        if (board.hasKeyboard())
                        {
                            s_saved_keyboard_brightness = board.keyboardGetBrightness();
                            board.keyboardSetBrightness(0);
                        }
                        board.setBrightness(0);
                        board.enterScreenSleep();
                    }
                    else if (s_screen_sleeping && time_since_activity < current_timeout)
                    {
                        s_screen_sleeping = false;
                        board.exitScreenSleep();
                        board.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
                        if (board.hasKeyboard())
                        {
                            board.keyboardSetBrightness(s_saved_keyboard_brightness);
                        }
                    }

                    xSemaphoreGive(s_activity_mutex);
                }
            }
        }

        vTaskDelayUntil(&last_wake_time, check_interval);
    }
}

} // namespace

uint32_t clampScreenTimeoutMs(uint32_t timeout_ms)
{
    if (timeout_ms < kScreenTimeoutMinMs)
    {
        return kScreenTimeoutDefaultMs;
    }
    if (timeout_ms > kScreenTimeoutMaxMs)
    {
        return kScreenTimeoutMaxMs;
    }
    return timeout_ms;
}

uint32_t getScreenSleepTimeout()
{
    if (!isScreenTimeoutLoaded())
    {
        cacheScreenTimeoutMs(readPersistedScreenTimeoutMs());
    }
    return cachedScreenTimeoutMs();
}

uint16_t readScreenTimeoutSecs()
{
    uint32_t secs = getScreenSleepTimeout() / 1000U;
    if (secs > kScreenTimeoutMaxBleSecs)
    {
        secs = kScreenTimeoutMaxBleSecs;
    }
    return static_cast<uint16_t>(secs);
}

void setScreenSleepTimeout(uint32_t timeout_ms)
{
    timeout_ms = clampScreenTimeoutMs(timeout_ms);
    writePersistedScreenTimeoutMs(timeout_ms);
    cacheScreenTimeoutMs(timeout_ms);
}

void initScreenSleepRuntime(const ScreenSleepHooks& hooks)
{
    s_hooks = hooks;
    init_screen_saver();

    if (s_activity_mutex == nullptr)
    {
        s_activity_mutex = xSemaphoreCreateMutex();
        if (s_activity_mutex == nullptr)
        {
            log_e("Failed to create activity mutex");
            return;
        }

        s_last_user_activity_time = millis();
        (void)getScreenSleepTimeout();
    }

    if (s_screen_sleep_task_handle == nullptr)
    {
        BaseType_t sleep_task_result = xTaskCreate(
            screenSleepTask,
            "screen_sleep",
            2 * 1024,
            nullptr,
            3,
            &s_screen_sleep_task_handle);
        if (sleep_task_result != pdPASS)
        {
            log_e("Failed to create screen sleep task");
            s_screen_sleep_task_handle = nullptr;
        }
        else
        {
            log_d("Screen sleep management task created successfully");
        }
    }
}

bool isScreenSleeping()
{
    bool sleeping = false;
    if (s_activity_mutex != nullptr)
    {
        if (xSemaphoreTake(s_activity_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            sleeping = s_screen_sleeping;
            xSemaphoreGive(s_activity_mutex);
        }
    }
    return sleeping;
}

bool isScreenSaverActive()
{
    bool active = false;
    if (s_activity_mutex != nullptr)
    {
        if (xSemaphoreTake(s_activity_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            active = s_screen_saver_active;
            xSemaphoreGive(s_activity_mutex);
        }
    }
    return active;
}

void wakeScreenSaver()
{
    if (s_screen_sleep_disabled)
    {
        updateUserActivity();
        return;
    }

    if (!s_screen_saver_layer)
    {
        return;
    }

    if (s_activity_mutex != nullptr)
    {
        if (xSemaphoreTake(s_activity_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            s_screen_saver_active = true;
            s_screen_sleeping = true;
            xSemaphoreGive(s_activity_mutex);
        }
    }

    screen_saver_refresh();
    lv_obj_clear_flag(s_screen_saver_layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_screen_saver_layer);
    lv_refr_now(nullptr);
    board.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    if (s_screen_saver_timer == nullptr)
    {
        s_screen_saver_timer = lv_timer_create(screen_saver_timer_cb, kScreenSaverDurationMs, nullptr);
    }
    else
    {
        lv_timer_set_period(s_screen_saver_timer, kScreenSaverDurationMs);
        lv_timer_reset(s_screen_saver_timer);
        lv_timer_resume(s_screen_saver_timer);
    }
}

void enterFromScreenSaver()
{
    if (!isScreenSaverActive())
    {
        return;
    }

    if (s_activity_mutex != nullptr)
    {
        if (xSemaphoreTake(s_activity_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            s_screen_saver_active = false;
            s_screen_sleeping = false;
            s_last_user_activity_time = millis();
            xSemaphoreGive(s_activity_mutex);
        }
    }

    hide_screen_saver_layer();
    updateUserActivity();
    showMainMenu();
}

void disableScreenSleep()
{
    bool hide_saver = false;
    if (s_activity_mutex != nullptr)
    {
        if (xSemaphoreTake(s_activity_mutex, portMAX_DELAY) == pdTRUE)
        {
            s_screen_sleep_disabled = true;
            if (s_screen_saver_active)
            {
                s_screen_saver_active = false;
                hide_saver = true;
            }
            if (s_screen_sleeping)
            {
                s_screen_sleeping = false;
                board.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
                if (board.hasKeyboard())
                {
                    board.keyboardSetBrightness(s_saved_keyboard_brightness);
                }
            }
            xSemaphoreGive(s_activity_mutex);
        }
    }
    if (hide_saver)
    {
        hide_screen_saver_layer();
    }
}

void enableScreenSleep()
{
    if (s_activity_mutex != nullptr)
    {
        if (xSemaphoreTake(s_activity_mutex, portMAX_DELAY) == pdTRUE)
        {
            s_screen_sleep_disabled = false;
            s_last_user_activity_time = millis();
            xSemaphoreGive(s_activity_mutex);
        }
    }
}

bool isScreenSleepDisabled()
{
    bool disabled = false;
    if (s_activity_mutex != nullptr)
    {
        if (xSemaphoreTake(s_activity_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            disabled = s_screen_sleep_disabled;
            xSemaphoreGive(s_activity_mutex);
        }
    }
    return disabled;
}

void updateUserActivity()
{
    bool woke_from_sleep = false;
    bool hide_saver = false;
    if (s_activity_mutex != nullptr)
    {
        if (xSemaphoreTake(s_activity_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            s_last_user_activity_time = millis();
            if (s_screen_saver_active)
            {
                s_screen_saver_active = false;
                hide_saver = true;
            }
            if (s_screen_sleeping)
            {
                s_screen_sleeping = false;
                board.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
                if (board.hasKeyboard())
                {
                    board.keyboardSetBrightness(s_saved_keyboard_brightness);
                }
                woke_from_sleep = true;
            }
            xSemaphoreGive(s_activity_mutex);
        }
    }
    if (hide_saver)
    {
        hide_screen_saver_layer();
    }
    if (woke_from_sleep)
    {
        notifyWakeFromSleep();
    }
}

/**
 * @file screen_sleep.cpp
 * @brief Arduino adapter for the shared screen-power state machine.
 */

#include "screen_sleep.h"

#include <cstdint>
#include <cstdio>

#include "board/BoardBase.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "platform/ui/screen_power_state_machine.h"
#include "platform/ui/screen_runtime.h"
#include "platform/ui/settings_store.h"

namespace
{

using platform::ui::screen_power::Effects;
using platform::ui::screen_power::Event;
using platform::ui::screen_power::Snapshot;
using platform::ui::screen_power::State;
using platform::ui::screen_power::StateMachine;

constexpr const char* kSettingsNs = "settings";
constexpr const char* kScreenTimeoutKey = "screen_timeout";
constexpr std::uint32_t kQueueDepth = 32;
constexpr std::uint32_t kDrainPeriodMs = 100;

ScreenSleepHooks s_hooks{};
StateMachine s_machine{};
SemaphoreHandle_t s_state_mutex = nullptr;
QueueHandle_t s_event_queue = nullptr;
lv_timer_t* s_drain_timer = nullptr;
std::uint8_t s_saved_screen_brightness = DEVICE_MAX_BRIGHTNESS_LEVEL;
std::uint8_t s_saved_keyboard_brightness = 127;

std::uint32_t now_ms()
{
    return millis();
}

void ensure_state_mutex()
{
    if (s_state_mutex == nullptr)
    {
        s_state_mutex = xSemaphoreCreateMutex();
    }
}

void ensure_event_queue()
{
    if (s_event_queue == nullptr)
    {
        s_event_queue = xQueueCreate(kQueueDepth, sizeof(Event));
    }
}

void apply_effects(const Effects& effects)
{
    const bool retain_saver_during_sleep =
        effects.sleep_display && board.keepsScreenSaverVisibleDuringSleep();

    if (effects.sleep_display)
    {
        s_saved_screen_brightness = board.getBrightness();
        if (board.hasKeyboard())
        {
            s_saved_keyboard_brightness = board.keyboardGetBrightness();
            board.keyboardSetBrightness(0);
        }
        board.setBrightness(0);
        board.enterScreenSleep();

        if (retain_saver_during_sleep && s_hooks.show_screen_saver)
        {
            s_hooks.show_screen_saver();
        }
        if (retain_saver_during_sleep && s_hooks.present_screen_saver)
        {
            s_hooks.present_screen_saver();
        }
    }

    if (effects.wake_display)
    {
        board.exitScreenSleep();
        board.setBrightness(s_saved_screen_brightness);
        if (board.hasKeyboard())
        {
            board.keyboardSetBrightness(s_saved_keyboard_brightness);
        }
    }

    if (effects.hide_saver && !retain_saver_during_sleep && s_hooks.hide_screen_saver)
    {
        s_hooks.hide_screen_saver();
    }

    if (effects.show_saver)
    {
        if (s_hooks.show_screen_saver)
        {
            s_hooks.show_screen_saver();
        }
        else if (effects.notify_wake && s_hooks.on_wake_from_sleep)
        {
            s_hooks.on_wake_from_sleep();
        }
        if (s_hooks.present_screen_saver)
        {
            s_hooks.present_screen_saver();
        }
    }

    if (effects.show_main_menu && s_hooks.show_main_menu)
    {
        s_hooks.show_main_menu();
    }
}

void dispatch_event(Event event)
{
    Effects effects{};
    const Snapshot before = s_machine.snapshot();
    ensure_state_mutex();
    if (s_state_mutex == nullptr ||
        xSemaphoreTake(s_state_mutex, portMAX_DELAY) != pdTRUE)
    {
        return;
    }
    effects = s_machine.dispatch(event, now_ms());
    xSemaphoreGive(s_state_mutex);
    if (before.state != s_machine.snapshot().state || effects.wake_display || effects.sleep_display ||
        effects.show_saver || effects.show_main_menu)
    {
        const Snapshot after = s_machine.snapshot();
        std::printf("[ScreenPower] event=%u state=%u->%u effects wake=%u sleep=%u saver=%u menu=%u\n",
                    static_cast<unsigned>(event),
                    static_cast<unsigned>(before.state),
                    static_cast<unsigned>(after.state),
                    effects.wake_display ? 1U : 0U,
                    effects.sleep_display ? 1U : 0U,
                    effects.show_saver ? 1U : 0U,
                    effects.show_main_menu ? 1U : 0U);
    }
    apply_effects(effects);
}

void drain_events()
{
    if (s_event_queue == nullptr)
    {
        return;
    }

    Event event = Event::Tick;
    while (xQueueReceive(s_event_queue, &event, 0) == pdTRUE)
    {
        dispatch_event(event);
    }
    dispatch_event(Event::Tick);
}

void drain_timer_cb(lv_timer_t* /*timer*/)
{
    drain_events();
}

bool post_event(Event event)
{
    ensure_event_queue();
    if (s_event_queue == nullptr)
    {
        return false;
    }
    return xQueueSend(s_event_queue, &event, 0) == pdTRUE;
}

Snapshot snapshot()
{
    Snapshot value{};
    ensure_state_mutex();
    if (s_state_mutex != nullptr &&
        xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE)
    {
        value = s_machine.snapshot();
        xSemaphoreGive(s_state_mutex);
    }
    return value;
}

void initialize_state()
{
    ensure_state_mutex();
    ensure_event_queue();
    if (s_state_mutex == nullptr)
    {
        return;
    }
    if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE)
    {
        const std::uint32_t persisted_timeout =
            platform::ui::settings_store::get_uint(
                kSettingsNs,
                kScreenTimeoutKey,
                StateMachine::kDefaultTimeoutMs);
        s_machine.set_timeout_ms(persisted_timeout);
        (void)s_machine.dispatch(Event::Initialize, now_ms());
        xSemaphoreGive(s_state_mutex);
    }
}

} // namespace

uint32_t clampScreenTimeoutMs(uint32_t timeout_ms)
{
    return StateMachine::clamp_timeout_ms(timeout_ms);
}

uint32_t getScreenSleepTimeout()
{
    ensure_state_mutex();
    if (s_state_mutex != nullptr &&
        xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE)
    {
        const std::uint32_t timeout_ms = s_machine.timeout_ms();
        xSemaphoreGive(s_state_mutex);
        return timeout_ms;
    }
    return StateMachine::kDefaultTimeoutMs;
}

uint16_t readScreenTimeoutSecs()
{
    const std::uint32_t seconds = getScreenSleepTimeout() / 1000U;
    return static_cast<uint16_t>(seconds > 900U ? 900U : seconds);
}

void setScreenSleepTimeout(uint32_t timeout_ms)
{
    const std::uint32_t clamped = clampScreenTimeoutMs(timeout_ms);
    platform::ui::settings_store::put_uint(kSettingsNs, kScreenTimeoutKey, clamped);
    ensure_state_mutex();
    if (s_state_mutex != nullptr &&
        xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE)
    {
        s_machine.set_timeout_ms(clamped);
        xSemaphoreGive(s_state_mutex);
    }
}

void initScreenSleepRuntime(const ScreenSleepHooks& hooks)
{
    s_hooks = hooks;
    initialize_state();
    if (s_drain_timer == nullptr)
    {
        s_drain_timer = lv_timer_create(drain_timer_cb, kDrainPeriodMs, nullptr);
    }
}

bool isScreenSleeping()
{
    return snapshot().state == State::Sleeping;
}

bool isScreenSaverActive()
{
    return snapshot().state == State::WakePreview;
}

namespace platform::ui::screen
{

uint32_t clamp_timeout_ms(uint32_t timeout_ms)
{
    return clampScreenTimeoutMs(timeout_ms);
}

uint32_t timeout_ms()
{
    return getScreenSleepTimeout();
}

uint16_t timeout_secs()
{
    return readScreenTimeoutSecs();
}

bool supports_app_timeout_setting()
{
    return true;
}

void set_timeout_ms(uint32_t timeout_ms)
{
    setScreenSleepTimeout(timeout_ms);
}

void init(const Hooks& hooks)
{
    ScreenSleepHooks adapted{};
    adapted.format_time = hooks.format_time;
    adapted.read_unread_count = hooks.read_unread_count;
    adapted.show_main_menu = hooks.show_main_menu;
    adapted.on_wake_from_sleep = hooks.on_wake_from_sleep;
    adapted.show_screen_saver = hooks.show_screen_saver;
    adapted.hide_screen_saver = hooks.hide_screen_saver;
    adapted.present_screen_saver = hooks.present_screen_saver;
    initScreenSleepRuntime(adapted);
}

bool is_sleeping()
{
    return isScreenSleeping();
}

bool is_sleep_disabled()
{
    return snapshot().sleep_disable_depth != 0;
}

bool is_saver_active()
{
    return isScreenSaverActive();
}

void handle_input()
{
    (void)post_event(Event::Input);
}

void handle_confirm_input()
{
    request_resume();
}

void handle_input_release()
{
    (void)post_event(Event::InputRelease);
}

void wake_for_modal()
{
    (void)post_event(Event::ModalWake);
}

void record_activity()
{
    (void)post_event(Event::Activity);
}

void disable_sleep()
{
    (void)post_event(Event::DisableSleep);
}

void enable_sleep()
{
    (void)post_event(Event::EnableSleep);
}

void request_resume()
{
    (void)post_event(Event::ConfirmInput);
}

ResumeMethod resume_method()
{
#if defined(ARDUINO_WIO_TRACKER_L2)
    return ResumeMethod::SwipeUp;
#else
    return ResumeMethod::SpaceKey;
#endif
}

} // namespace platform::ui::screen

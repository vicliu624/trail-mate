#include "platform/ui/screen_runtime.h"

#include "screen_sleep.h"

namespace
{

ScreenSleepHooks adapt_hooks(const platform::ui::screen::Hooks& hooks)
{
    ScreenSleepHooks adapted{};
    adapted.format_time = hooks.format_time;
    adapted.read_unread_count = hooks.read_unread_count;
    adapted.show_main_menu = hooks.show_main_menu;
    adapted.on_wake_from_sleep = hooks.on_wake_from_sleep;
    return adapted;
}

} // namespace

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

void set_timeout_ms(uint32_t timeout_ms)
{
    setScreenSleepTimeout(timeout_ms);
}

void init(const Hooks& hooks)
{
    initScreenSleepRuntime(adapt_hooks(hooks));
}

bool is_sleeping()
{
    return isScreenSleeping();
}

bool is_sleep_disabled()
{
    return isScreenSleepDisabled();
}

bool is_saver_active()
{
    return isScreenSaverActive();
}

void wake_saver()
{
    wakeScreenSaver();
}

void enter_from_saver()
{
    enterFromScreenSaver();
}

void update_user_activity()
{
    updateUserActivity();
}

void disable_sleep()
{
    disableScreenSleep();
}

void enable_sleep()
{
    enableScreenSleep();
}

} // namespace platform::ui::screen

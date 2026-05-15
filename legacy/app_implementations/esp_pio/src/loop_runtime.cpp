#include "apps/esp_pio/loop_runtime.h"

#include <Arduino.h>

#include "apps/esp_pio/app_runtime_access.h"
#include "board/BoardBase.h"
#include "platform/esp/arduino_common/display_runtime.h"
#include "ui/app_runtime.h"
#include "ui/loop_shell.h"

#ifndef MAIN_TIMING_DEBUG
#define MAIN_TIMING_DEBUG 0
#endif

namespace
{

#if MAIN_TIMING_DEBUG
uint32_t s_last_loop_ms = 0;
uint32_t s_loop_count = 0;
#endif

void log_loop_interval(uint32_t now_ms)
{
#if MAIN_TIMING_DEBUG
    if (s_last_loop_ms > 0)
    {
        const uint32_t interval = now_ms - s_last_loop_ms;
        if (interval > 50)
        {
            Serial.printf("[MAIN] loop() interval: %lu ms (count=%lu)\n", interval, s_loop_count);
        }
    }

    s_last_loop_ms = now_ms;
    s_loop_count++;
#else
    (void)now_ms;
#endif
}

} // namespace

namespace apps::esp_pio::loop_runtime
{

void tick()
{
    ui::loop_shell::Hooks hooks{};
    hooks.now_ms = []() -> uint32_t
    { return millis(); };
    hooks.is_overlay_active = []() -> bool
    {
        return ui_is_overlay_active();
    };
    hooks.handle_power_button = []()
    { board.handlePowerButton(); };
    hooks.update_runtime = []()
    { apps::esp_pio::app_runtime_access::tick(); };
    hooks.display_tick_if_due = [](uint32_t now_ms)
    {
        log_loop_interval(now_ms);
        platform::esp::arduino_common::display_runtime::tickIfDue(now_ms);
    };
    hooks.yield_now = []()
    { yield(); };
    hooks.sleep_ms = [](uint32_t ms)
    { delay(ms); };
    hooks.overlay_sleep_ms = 10;
    hooks.idle_sleep_ms = 2;
    ui::loop_shell::tick(hooks);
}

} // namespace apps::esp_pio::loop_runtime

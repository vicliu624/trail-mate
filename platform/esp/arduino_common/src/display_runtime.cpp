#include "platform/esp/arduino_common/display_runtime.h"

#include <Arduino.h>

#include "lvgl.h"
#include "platform/esp/boards/board_runtime.h"

#ifndef MAIN_TIMING_DEBUG
#define MAIN_TIMING_DEBUG 0
#endif

namespace
{

constexpr uint32_t kLvglIntervalMs = 20;
uint32_t s_last_lvgl_ms = 0;

} // namespace

namespace platform::esp::arduino_common::display_runtime
{

void initialize()
{
    platform::esp::boards::initializeDisplay();
}

void tickIfDue(uint32_t now_ms)
{
    const bool run_lvgl = (now_ms - s_last_lvgl_ms >= kLvglIntervalMs);
#if MAIN_TIMING_DEBUG
    uint32_t t_before = 0;
#endif

    if (run_lvgl)
    {
        s_last_lvgl_ms = now_ms;
#if MAIN_TIMING_DEBUG
        t_before = millis();
#endif
        lv_timer_handler();
    }

#if MAIN_TIMING_DEBUG
    if (run_lvgl)
    {
        const uint32_t t_after = millis();
        const uint32_t handler_duration = t_after - t_before;
        if (handler_duration > 10)
        {
            Serial.printf("[MAIN] lv_timer_handler() took %lu ms\n", handler_duration);
        }
    }
#endif
}

} // namespace platform::esp::arduino_common::display_runtime

/**
 * @file ui_dispatcher.h
 * @brief IDF UI event dispatcher – serialises UI calls from non-LVGL tasks.
 *
 * Problem: screen_sleep, touch callbacks, and other FreeRTOS tasks were
 * directly calling LVGL UI functions (showWatchFace, showMainMenu, etc.)
 * from outside the LVGL task context.  This caused races, deadlocks, and
 * UI corruption on Tab5 / T-Display P4.
 *
 * Solution: Non-UI tasks post events to a lock-free queue.  An LVGL timer
 * drains the queue in the LVGL task context, where it is safe to touch
 * the widget tree.
 */

#pragma once

#include <cstdint>

namespace platform::esp::idf_common::ui_dispatcher
{

enum class Event : std::uint8_t
{
    None = 0,
    WakeFromSleep, // → onWakeFromSleep → showWatchFace
    ShowMainMenu,  // → showMainMenu
};

struct Hooks
{
    void (*on_wake_from_sleep)() = nullptr;
    void (*show_main_menu)() = nullptr;
};

/** One-time initialisation.  Must be called before any post/drain. */
void init(const Hooks& hooks);

/** Post an event from any task / ISR-compatible context.  Non-blocking. */
bool post(Event event);

/** Drain all pending events.  MUST only be called from the LVGL task. */
void drain();

/** Create an LVGL timer that calls drain() every period_ms milliseconds.
 *  Returns the timer handle so it can be paused/deleted if needed.
 *  Safe to call multiple times (subsequent calls are no-ops). */
void* ensure_drain_timer(std::uint32_t period_ms = 100);

} // namespace platform::esp::idf_common::ui_dispatcher

/**
 * @file screen_sleep.h
 * @brief Screen sleep timeout/runtime API shared by main runtime, BLE, and settings UI.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

struct ScreenSleepHooks
{
    bool (*format_time)(char* out, size_t out_len) = nullptr;
    int (*read_unread_count)() = nullptr;
    void (*show_main_menu)() = nullptr;
    void (*on_wake_from_sleep)() = nullptr;
};

/** Clamp timeout into the allowed persisted range. Values below the minimum fall back to the default. */
uint32_t clampScreenTimeoutMs(uint32_t timeout_ms);

/** Current screen sleep timeout in milliseconds. */
uint32_t getScreenSleepTimeout();

/** Current screen sleep timeout in whole seconds, capped to the BLE/UI range. */
uint16_t readScreenTimeoutSecs();

/** Set screen sleep timeout, persist it, and refresh the runtime cache. */
void setScreenSleepTimeout(uint32_t timeout_ms);

/** Initialize the screen-sleep runtime shell and start its management task. */
void initScreenSleepRuntime(const ScreenSleepHooks& hooks);

/** True when the screen backlight/runtime is currently sleeping. */
bool isScreenSleeping();

/** True when automatic screen sleep is temporarily disabled. */
bool isScreenSleepDisabled();

/** True when the short-lived screen saver layer is active. */
bool isScreenSaverActive();

/** Wake the screen / dismiss the transient saver without entering the UI. */
void wakeScreenSaver();

/** Enter the UI from the transient screen saver state. */
void enterFromScreenSaver();

/** Refresh the user-activity timestamp and wake the screen if needed. */
void updateUserActivity();

/** Temporarily disable automatic screen sleep. */
void disableScreenSleep();

/** Re-enable automatic screen sleep. */
void enableScreenSleep();

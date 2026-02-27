/**
 * @file screen_sleep.h
 * @brief Screen sleep timeout API (implemented in main.cpp).
 * Allows BLE and settings UI to apply timeout changes at runtime.
 */

#pragma once

#include <stdint.h>

/** Current screen sleep timeout in milliseconds */
uint32_t getScreenSleepTimeout();

/** Set screen sleep timeout (saves to prefs and updates runtime). Value is clamped to allowed range. */
void setScreenSleepTimeout(uint32_t timeout_ms);

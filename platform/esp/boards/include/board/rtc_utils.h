/**
 * @file rtc_utils.h
 * @brief RTC helper functions (lightweight, no Wire include)
 */

#pragma once

#include <stdint.h>

/**
 * @brief Adjust RTC time by offset minutes (e.g., timezone change)
 * @param offset_minutes Minutes to add (negative to subtract)
 * @return true if RTC updated successfully
 */
bool board_adjust_rtc_by_offset_minutes(int offset_minutes);

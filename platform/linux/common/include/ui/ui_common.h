/**
 * @file ui_common.h
 * @brief Linux-safe UI helpers used by shared UI code.
 */

#ifndef UI_COMMON_H
#define UI_COMMON_H

#include <ctime>

#include "lvgl.h"

namespace ui::widgets
{
struct TopBar;
}

void ui_update_top_bar_battery(ui::widgets::TopBar& bar);

int ui_get_timezone_offset_min();
void ui_set_timezone_offset_min(int offset_min);
time_t ui_apply_timezone_offset(time_t utc_seconds);

bool ui_take_screenshot_to_sd();

#endif // UI_COMMON_H

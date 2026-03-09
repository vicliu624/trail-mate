/**
 * @file ui_common.h
 * @brief Common UI functions and declarations
 */

#ifndef UI_COMMON_H
#define UI_COMMON_H

#include "lvgl.h"
#include "ui/app_runtime.h"
#include "ui/formatters.h"
#include "ui/widgets/top_bar.h"
#include <ctime>

// Update a shared TopBar's right-side battery text from board state
void ui_update_top_bar_battery(ui::widgets::TopBar& bar);

// Timezone offset (minutes) for display-only local time
int ui_get_timezone_offset_min();
void ui_set_timezone_offset_min(int offset_min);
time_t ui_apply_timezone_offset(time_t utc_seconds);

// Screenshot helper (BMP, RGB565) saved to /sd
bool ui_take_screenshot_to_sd();

#endif // UI_COMMON_H

/**
 * @file ui_common.h
 * @brief Common UI functions and declarations
 */

#ifndef UI_COMMON_H
#define UI_COMMON_H

#include "lvgl.h"
#include "widgets/top_bar.h"

// Forward declarations
extern lv_obj_t *main_screen;
extern lv_group_t *menu_g;

void menu_show();
void set_default_group(lv_group_t *group);

// Menu creation helper (simplified version of factory's create_menu)
lv_obj_t *create_menu(lv_obj_t *parent, lv_event_cb_t event_cb);

// Battery/status formatting helper used across UI screens (chat, GPS, main menu)
void ui_format_battery(int level, bool charging, char* out, size_t out_len);

// Update a shared TopBar's right-side battery text from board state
void ui_update_top_bar_battery(ui::widgets::TopBar& bar);

// Screenshot helper (BMP, RGB565) saved to /sd
bool ui_take_screenshot_to_sd();

#endif // UI_COMMON_H

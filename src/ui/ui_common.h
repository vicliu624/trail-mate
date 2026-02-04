/**
 * @file ui_common.h
 * @brief Common UI functions and declarations
 */

#ifndef UI_COMMON_H
#define UI_COMMON_H

#include "app_screen.h"
#include "lvgl.h"
#include "widgets/top_bar.h"
#include <ctime>

// Forward declarations
extern lv_obj_t* main_screen;
extern lv_group_t* menu_g;
extern lv_group_t* app_g;

void menu_show();
void ui_clear_active_app();
AppScreen* ui_get_active_app();
void ui_switch_to_app(AppScreen* app, lv_obj_t* parent);
void ui_exit_active_app(lv_obj_t* parent);
void ui_request_exit_to_menu();
void set_default_group(lv_group_t* group);

// Menu creation helper (simplified version of factory's create_menu)
lv_obj_t* create_menu(lv_obj_t* parent, lv_event_cb_t event_cb);

// Battery/status formatting helper used across UI screens (chat, GPS, main menu)
void ui_format_battery(int level, bool charging, char* out, size_t out_len);

// Update a shared TopBar's right-side battery text from board state
void ui_update_top_bar_battery(ui::widgets::TopBar& bar);

// Timezone offset (minutes) for display-only local time
int ui_get_timezone_offset_min();
void ui_set_timezone_offset_min(int offset_min);
time_t ui_apply_timezone_offset(time_t utc_seconds);

// Coordinate formatting helper (DD/DMS/UTM)
void ui_format_coords(double lat, double lon, uint8_t coord_format, char* out, size_t out_len);

// Screenshot helper (BMP, RGB565) saved to /sd
bool ui_take_screenshot_to_sd();

#endif // UI_COMMON_H

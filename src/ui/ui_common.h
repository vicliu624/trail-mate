/**
 * @file ui_common.h
 * @brief Common UI functions and declarations
 */

#ifndef UI_COMMON_H
#define UI_COMMON_H

#include "lvgl.h"

// Forward declarations
extern lv_obj_t *main_screen;
extern lv_group_t *menu_g;

void menu_show();
void set_default_group(lv_group_t *group);

// Menu creation helper (simplified version of factory's create_menu)
lv_obj_t *create_menu(lv_obj_t *parent, lv_event_cb_t event_cb);

#endif // UI_COMMON_H

#pragma once

#include "lvgl.h"
#include "ui/app_screen.h"

extern lv_obj_t* main_screen;
extern lv_group_t* menu_g;
extern lv_group_t* app_g;

void menu_show();
void ui_clear_active_app();
AppScreen* ui_get_active_app();
void ui_switch_to_app(AppScreen* app, lv_obj_t* parent);
void ui_exit_active_app(lv_obj_t* parent);
bool ui_present_interruption_app(AppScreen* app, lv_obj_t* parent);
void ui_dismiss_interruption_app(lv_obj_t* parent);
bool ui_is_interruption_app_active();
// Global modal renderers must wait until a queued exit/rebuild has completed.
bool ui_is_transition_pending();
void ui_request_exit_to_menu();
void ui_request_rebuild_active_app();
void ui_set_overlay_active(bool active);
bool ui_is_overlay_active();
void set_default_group(lv_group_t* group);

lv_obj_t* create_menu(lv_obj_t* parent, lv_event_cb_t event_cb);

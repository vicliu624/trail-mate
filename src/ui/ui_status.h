/**
 * @file ui_status.h
 * @brief Global UI status indicators (top bar icons + menu badges)
 */

#pragma once

#include "lvgl.h"

namespace ui
{
namespace status
{

void init();
void register_menu_status_row(lv_obj_t* row,
                              lv_obj_t* route_icon,
                              lv_obj_t* tracker_icon,
                              lv_obj_t* gps_icon,
                              lv_obj_t* team_icon,
                              lv_obj_t* msg_icon);
void register_chat_badge(lv_obj_t* badge_bg, lv_obj_t* badge_label);
void force_update();

} // namespace status
} // namespace ui

/**
 * @file team_state.h
 * @brief Team page UI state
 */

#pragma once

#include "lvgl.h"
#include "../../widgets/top_bar.h"

namespace team
{
namespace ui
{

struct TeamPageState
{
    lv_obj_t* root = nullptr;
    lv_obj_t* page = nullptr;
    lv_obj_t* header = nullptr;
    lv_obj_t* content = nullptr;

    lv_obj_t* state_container = nullptr;
    lv_obj_t* idle_container = nullptr;
    lv_obj_t* active_container = nullptr;

    lv_obj_t* idle_status_label = nullptr;
    lv_obj_t* create_btn = nullptr;
    lv_obj_t* join_btn = nullptr;

    lv_obj_t* active_status_label = nullptr;
    lv_obj_t* share_btn = nullptr;
    lv_obj_t* leave_btn = nullptr;
    lv_obj_t* disband_btn = nullptr;
    const char* title_override = nullptr;

    ::ui::widgets::TopBar top_bar_widget;
    lv_group_t* group = nullptr;
};

extern TeamPageState g_team_state;

} // namespace ui
} // namespace team

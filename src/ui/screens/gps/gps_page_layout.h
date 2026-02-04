#pragma once

#include "../../widgets/top_bar.h"
#include "lvgl.h"

namespace gps::ui::layout
{

struct Spec
{
    int panel_width = 85;
    int panel_top_offset = 3;
    int panel_row_gap = 3;
    int member_panel_width = 90;
    int member_panel_top_offset = 3;
    int member_panel_left_offset = 0;
    int member_btn_w = 86;
    int member_btn_h = 28;

    int resolution_pad = 4;
    int resolution_x = 10;
    int resolution_y = -10;

    int control_btn_w = 80;
    int control_btn_h = 28;
};

struct Widgets
{
    lv_obj_t* root = nullptr;
    lv_obj_t* header = nullptr;
    lv_obj_t* content = nullptr;
    lv_obj_t* map = nullptr;

    lv_obj_t* resolution_label = nullptr;

    lv_obj_t* panel = nullptr;
    lv_obj_t* member_panel = nullptr;
    lv_obj_t* zoom_btn = nullptr;
    lv_obj_t* zoom_label = nullptr;
    lv_obj_t* pos_btn = nullptr;
    lv_obj_t* pos_label = nullptr;
    lv_obj_t* pan_h_btn = nullptr;
    lv_obj_t* pan_h_label = nullptr;
    lv_obj_t* pan_v_btn = nullptr;
    lv_obj_t* pan_v_label = nullptr;
    lv_obj_t* tracker_btn = nullptr;
    lv_obj_t* tracker_label = nullptr;
    lv_obj_t* route_btn = nullptr;
    lv_obj_t* route_label = nullptr;

    ::ui::widgets::TopBar top_bar;
};

void create(lv_obj_t* parent, const Spec& spec, Widgets& w);

} // namespace gps::ui::layout

#pragma once
#include "../../widgets/top_bar.h"
#include "lvgl.h"

namespace chat::ui::compose::layout
{

struct Spec
{
    int action_bar_h = 32;
    int action_pad_lr = 10;
    int action_pad_tb = 2;

    int content_pad = 8;
    int content_row_pad = 4;

    int btn_h = 28;
    int send_w = 70;
    int position_w = 80;
    int cancel_w = 80;
    int btn_gap = 10;
};

struct Widgets
{
    lv_obj_t* container = nullptr;
    lv_obj_t* content = nullptr;
    lv_obj_t* textarea = nullptr;

    lv_obj_t* action_bar = nullptr;
    lv_obj_t* send_btn = nullptr;
    lv_obj_t* position_btn = nullptr;
    lv_obj_t* cancel_btn = nullptr;
    lv_obj_t* len_label = nullptr;

    ::ui::widgets::TopBar top_bar;
};

void create(lv_obj_t* parent, const Spec& spec, Widgets& w);

} // namespace chat::ui::compose::layout

#pragma once

#include "lvgl.h"
#include "../../widgets/top_bar.h"

namespace tracker
{
namespace ui
{

struct TrackerPageState
{
    lv_obj_t* root = nullptr;
    lv_obj_t* header = nullptr;
    lv_obj_t* content = nullptr;
    lv_obj_t* list = nullptr;
    lv_obj_t* start_stop_btn = nullptr;
    lv_obj_t* start_stop_label = nullptr;
    ::ui::widgets::TopBar top_bar{};
    bool initialized = false;
};

extern TrackerPageState g_tracker_state;

} // namespace ui
} // namespace tracker


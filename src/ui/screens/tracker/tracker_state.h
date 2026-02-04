#pragma once

#include "../../widgets/top_bar.h"
#include "lvgl.h"
#include <array>
#include <string>

namespace tracker
{
namespace ui
{

struct TrackerPageState
{
    enum class Mode : uint8_t
    {
        Record = 0,
        Route = 1
    };

    lv_obj_t* root = nullptr;
    lv_obj_t* header = nullptr;
    lv_obj_t* body = nullptr;
    lv_obj_t* mode_panel = nullptr;
    lv_obj_t* mode_record_btn = nullptr;
    lv_obj_t* mode_record_label = nullptr;
    lv_obj_t* mode_route_btn = nullptr;
    lv_obj_t* mode_route_label = nullptr;
    lv_obj_t* main_panel = nullptr;

    lv_obj_t* record_panel = nullptr;
    lv_obj_t* record_status_label = nullptr;
    lv_obj_t* start_stop_btn = nullptr;
    lv_obj_t* start_stop_label = nullptr;
    lv_obj_t* record_list = nullptr;
    std::array<lv_obj_t*, 4> record_item_btns{};
    std::array<lv_obj_t*, 4> record_item_labels{};
    lv_obj_t* record_prev_btn = nullptr;
    lv_obj_t* record_prev_label = nullptr;
    lv_obj_t* record_next_btn = nullptr;
    lv_obj_t* record_next_label = nullptr;
    int record_page = 0;

    lv_obj_t* route_panel = nullptr;
    lv_obj_t* route_status_label = nullptr;
    lv_obj_t* route_list = nullptr;
    std::array<lv_obj_t*, 4> route_item_btns{};
    std::array<lv_obj_t*, 4> route_item_labels{};
    lv_obj_t* route_prev_btn = nullptr;
    lv_obj_t* route_prev_label = nullptr;
    lv_obj_t* route_next_btn = nullptr;
    lv_obj_t* route_next_label = nullptr;
    int route_page = 0;
    lv_obj_t* load_btn = nullptr;
    lv_obj_t* load_label = nullptr;
    lv_obj_t* unload_btn = nullptr;
    lv_obj_t* unload_label = nullptr;
    ::ui::widgets::TopBar top_bar{};
    Mode mode = Mode::Record;
    int selected_route_idx = -1;
    std::string selected_route{};
    std::string active_route{};
    bool initialized = false;
};

extern TrackerPageState g_tracker_state;

} // namespace ui
} // namespace tracker

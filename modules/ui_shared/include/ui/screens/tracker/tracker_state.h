#pragma once

#include "lvgl.h"
#include "ui/widgets/top_bar.h"
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
    enum class FocusColumn : uint8_t
    {
        Mode = 0,
        Main = 1
    };

    lv_obj_t* root = nullptr;
    lv_obj_t* header = nullptr;
    lv_obj_t* content = nullptr;
    lv_obj_t* filter_panel = nullptr;
    lv_obj_t* selector_controls = nullptr;
    lv_obj_t* list_panel = nullptr;
    lv_obj_t* list_container = nullptr;
    lv_obj_t* bottom_bar = nullptr;

    lv_obj_t* mode_record_btn = nullptr;
    lv_obj_t* mode_record_label = nullptr;
    lv_obj_t* mode_route_btn = nullptr;
    lv_obj_t* mode_route_label = nullptr;

    lv_obj_t* status_label = nullptr;

    lv_obj_t* start_stop_btn = nullptr;
    lv_obj_t* start_stop_label = nullptr;

    lv_obj_t* del_confirm_modal = nullptr;
    lv_obj_t* action_menu_modal = nullptr;
    lv_group_t* modal_group = nullptr;
    lv_group_t* prev_group = nullptr;
    Mode pending_delete_mode = Mode::Record;
    std::string pending_delete_name{};
    std::string pending_delete_path{};

    ::ui::widgets::TopBar top_bar{};
    Mode mode = Mode::Record;
    FocusColumn focus_col = FocusColumn::Mode;
    int selected_record_idx = -1;
    std::string selected_record{};
    int selected_route_idx = -1;
    std::string selected_route{};
    std::string active_route{};
};

extern TrackerPageState g_tracker_state;

} // namespace ui
} // namespace tracker

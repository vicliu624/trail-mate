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
    enum class FocusColumn : uint8_t
    {
        Mode = 0,
        Main = 1
    };

    lv_obj_t* root = nullptr;
    lv_obj_t* header = nullptr;
    lv_obj_t* content = nullptr;
    lv_obj_t* filter_panel = nullptr;
    lv_obj_t* list_panel = nullptr;
    lv_obj_t* list_container = nullptr;
    lv_obj_t* bottom_bar = nullptr;
    lv_obj_t* action_panel = nullptr;

    lv_obj_t* mode_record_btn = nullptr;
    lv_obj_t* mode_record_label = nullptr;
    lv_obj_t* mode_route_btn = nullptr;
    lv_obj_t* mode_route_label = nullptr;

    lv_obj_t* status_label = nullptr;

    std::array<lv_obj_t*, 4> list_item_btns{};
    std::array<lv_obj_t*, 4> list_item_labels{};

    lv_obj_t* list_prev_btn = nullptr;
    lv_obj_t* list_prev_label = nullptr;
    lv_obj_t* list_next_btn = nullptr;
    lv_obj_t* list_next_label = nullptr;
    lv_obj_t* list_back_btn = nullptr;
    lv_obj_t* list_back_label = nullptr;

    lv_obj_t* start_stop_btn = nullptr;
    lv_obj_t* start_stop_label = nullptr;
    lv_obj_t* load_btn = nullptr;
    lv_obj_t* load_label = nullptr;
    lv_obj_t* unload_btn = nullptr;
    lv_obj_t* unload_label = nullptr;
    lv_obj_t* del_btn = nullptr;
    lv_obj_t* del_label = nullptr;
    lv_obj_t* action_back_btn = nullptr;
    lv_obj_t* action_back_label = nullptr;

    lv_obj_t* del_confirm_modal = nullptr;
    lv_group_t* modal_group = nullptr;
    lv_group_t* prev_group = nullptr;
    Mode pending_delete_mode = Mode::Record;
    int pending_delete_idx = -1;
    std::string pending_delete_name{};
    std::string pending_delete_path{};

    int record_page = 0;
    int route_page = 0;
    ::ui::widgets::TopBar top_bar{};
    Mode mode = Mode::Record;
    FocusColumn focus_col = FocusColumn::Mode;
    int selected_record_idx = -1;
    std::string selected_record{};
    int selected_route_idx = -1;
    std::string selected_route{};
    std::string active_route{};
    bool initialized = false;
};

extern TrackerPageState g_tracker_state;

} // namespace ui
} // namespace tracker

#pragma once

#include "lvgl.h"

namespace ui::presentation::menu_dashboard_layout
{

struct AppGridSpec
{
    lv_coord_t width = LV_PCT(100);
    int height_pct = 70;
    int top_offset = 30;
    int pad_row = 0;
    int pad_column = 0;
    int pad_left = 0;
    int pad_right = 0;
    lv_align_t align = LV_ALIGN_TOP_MID;
    lv_dir_t scroll_dir = LV_DIR_HOR;
    lv_flex_flow_t flow = LV_FLEX_FLOW_ROW;
    lv_flex_align_t main_align = LV_FLEX_ALIGN_START;
    lv_flex_align_t cross_align = LV_FLEX_ALIGN_START;
    lv_flex_align_t track_align = LV_FLEX_ALIGN_START;
    lv_scroll_snap_t snap_x = LV_SCROLL_SNAP_NONE;
};

struct AppListSpec
{
    lv_coord_t width = LV_PCT(100);
    int top_offset = 0;
    int pad_left = 8;
    int pad_right = 8;
    int pad_top = 8;
    int pad_bottom = 8;
    int pad_row = 6;
};

struct BottomChipsSpec
{
    lv_coord_t height = 30;
    int pad_left = 0;
    int pad_right = 0;
    int pad_top = 0;
    int pad_bottom = 0;
    int pad_column = 0;
};

void make_non_scrollable(lv_obj_t* obj);
lv_obj_t* create_app_grid_region(lv_obj_t* parent, const AppGridSpec& spec = {});
lv_obj_t* create_app_list_region(lv_obj_t* parent, const AppListSpec& spec = {});
lv_obj_t* create_bottom_chips_region(lv_obj_t* parent, const BottomChipsSpec& spec = {});

} // namespace ui::presentation::menu_dashboard_layout

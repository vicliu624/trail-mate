#pragma once

#include "lvgl.h"
#include "ui/components/two_pane_styles.h"

namespace ui::components::two_pane_layout
{

struct RootSpec
{
    int pad_row = 3;
};

struct HeaderSpec
{
    lv_coord_t height = 0;
    uint32_t bg_hex = two_pane_styles::kSidePanelBg;
    int pad_all = 0;
};

struct ContentSpec
{
    int pad_left = 0;
    int pad_right = 0;
    int pad_top = 0;
    int pad_bottom = 0;
};

struct SidePanelSpec
{
    lv_coord_t width = 80;
    int pad_row = 3;
    int margin_left = 0;
    int margin_right = 0;
    lv_scrollbar_mode_t scrollbar_mode = LV_SCROLLBAR_MODE_OFF;
};

struct MainPanelSpec
{
    int pad_all = 0;
    int pad_row = 3;
    int pad_left = 0;
    int pad_right = 0;
    int pad_top = 0;
    int pad_bottom = 0;
    int margin_left = 0;
    int margin_right = 0;
    int margin_bottom = 0;
    lv_scrollbar_mode_t scrollbar_mode = LV_SCROLLBAR_MODE_OFF;
};

void make_non_scrollable(lv_obj_t* obj);
lv_obj_t* create_root(lv_obj_t* parent, const RootSpec& spec = {});
lv_obj_t* create_header_container(lv_obj_t* parent, const HeaderSpec& spec);
lv_obj_t* create_content_row(lv_obj_t* parent, const ContentSpec& spec = {});
lv_obj_t* create_side_panel(lv_obj_t* parent, const SidePanelSpec& spec = {});
lv_obj_t* create_main_panel(lv_obj_t* parent, const MainPanelSpec& spec = {});

} // namespace ui::components::two_pane_layout

#pragma once

#include "lvgl.h"

#include <cstdint>

namespace ui::presentation::instrument_panel_layout
{

struct RootSpec
{
    lv_coord_t width = LV_PCT(100);
    lv_coord_t height = LV_PCT(100);
    int pad_row = 0;
    lv_opa_t bg_opa = LV_OPA_COVER;
    std::uint32_t bg_hex = 0xFFF3DF;
    lv_coord_t radius = 0;
};

struct HeaderSpec
{
    lv_coord_t height = 0;
};

struct BodySpec
{
    lv_flex_flow_t flow = LV_FLEX_FLOW_ROW;
    int pad_all = 0;
    int pad_row = 0;
    int pad_column = 0;
};

struct RegionSpec
{
    lv_coord_t width = LV_SIZE_CONTENT;
    lv_coord_t height = LV_SIZE_CONTENT;
    bool grow = false;
    int pad_all = 0;
    int pad_row = 0;
    int pad_column = 0;
    int margin_left = 0;
    int margin_right = 0;
    int margin_top = 0;
    int margin_bottom = 0;
    lv_scrollbar_mode_t scrollbar_mode = LV_SCROLLBAR_MODE_OFF;
    lv_opa_t bg_opa = LV_OPA_TRANSP;
    std::uint32_t bg_hex = 0;
};

enum class RegionRole : std::uint8_t
{
    Canvas = 0,
    Legend,
};

void make_non_scrollable(lv_obj_t* obj);
lv_obj_t* create_root(lv_obj_t* parent, const RootSpec& spec = {});
lv_obj_t* create_header_container(lv_obj_t* parent, const HeaderSpec& spec = {});
lv_obj_t* create_body(lv_obj_t* parent, const BodySpec& spec = {});
lv_obj_t* create_canvas_region(lv_obj_t* parent, const RegionSpec& spec = {});
lv_obj_t* create_legend_region(lv_obj_t* parent, const RegionSpec& spec = {});

} // namespace ui::presentation::instrument_panel_layout

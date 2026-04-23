#pragma once

#include "lvgl.h"

#include <cstdint>

namespace ui::presentation::map_focus_layout
{

struct RootSpec
{
    lv_coord_t width = LV_PCT(100);
    lv_coord_t height = LV_PCT(100);
    int pad_row = 0;
    lv_opa_t bg_opa = LV_OPA_TRANSP;
    std::uint32_t bg_hex = 0;
    lv_coord_t radius = 0;
};

struct HeaderSpec
{
    lv_coord_t height = 0;
};

struct ContentSpec
{
    int pad_all = 0;
};

struct ViewportSpec
{
    lv_coord_t width = LV_PCT(100);
    lv_coord_t height = LV_PCT(100);
    bool grow = true;
    bool clickable = true;
};

struct OverlayPanelSpec
{
    lv_coord_t width = LV_SIZE_CONTENT;
    lv_coord_t height = LV_SIZE_CONTENT;
    lv_align_t align = LV_ALIGN_TOP_RIGHT;
    lv_coord_t align_x = 0;
    lv_coord_t align_y = 0;
    lv_flex_flow_t flow = LV_FLEX_FLOW_COLUMN;
    int pad_all = 0;
    int pad_row = 0;
    int pad_column = 0;
    lv_scrollbar_mode_t scrollbar_mode = LV_SCROLLBAR_MODE_OFF;
    lv_opa_t bg_opa = LV_OPA_TRANSP;
    std::uint32_t bg_hex = 0;
};

enum class OverlayPanelRole : std::uint8_t
{
    Primary = 0,
    Secondary,
};

void make_non_scrollable(lv_obj_t* obj);
lv_obj_t* create_root(lv_obj_t* parent, const RootSpec& spec = {});
lv_obj_t* create_header_container(lv_obj_t* parent, const HeaderSpec& spec = {});
lv_obj_t* create_content(lv_obj_t* parent, const ContentSpec& spec = {});
lv_obj_t* create_viewport_region(lv_obj_t* parent, const ViewportSpec& spec = {});
lv_obj_t* create_overlay_panel(lv_obj_t* parent,
                               OverlayPanelRole role = OverlayPanelRole::Primary,
                               const OverlayPanelSpec& spec = {});

} // namespace ui::presentation::map_focus_layout

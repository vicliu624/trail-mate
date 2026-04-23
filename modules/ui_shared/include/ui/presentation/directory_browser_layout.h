#pragma once

#include "lvgl.h"
#include "ui/components/two_pane_styles.h"

#include <cstdint>

namespace ui::presentation::directory_browser_layout
{

// Builtin presentation renderer for directory-browser pages.
// The current implementation materializes the default split-sidebar layout,
// while keeping page code insulated from the legacy two_pane naming.

struct RootSpec
{
    int pad_row = 3;
};

struct HeaderSpec
{
    lv_coord_t height = 0;
    std::uint32_t bg_hex = ::ui::components::two_pane_styles::side_panel_bg_hex();
    int pad_all = 0;
};

struct BodySpec
{
    int pad_left = 0;
    int pad_right = 0;
    int pad_top = 0;
    int pad_bottom = 0;
};

struct SelectorPanelSpec
{
    lv_coord_t width = 80;
    int pad_all = -1;
    int pad_row = 3;
    int margin_left = 0;
    int margin_right = 0;
    int margin_top = 0;
    int margin_bottom = 0;
    lv_scrollbar_mode_t scrollbar_mode = LV_SCROLLBAR_MODE_OFF;
    lv_opa_t bg_opa = LV_OPA_TRANSP;
    std::uint32_t bg_hex = 0;
};

struct SelectorControlsSpec
{
    int pad_all = 0;
    int pad_row = 3;
    int pad_column = 3;
    int margin_top = 0;
    int margin_bottom = 0;
};

struct SelectorButtonSpec
{
    lv_coord_t height = 28;
    lv_coord_t split_width = LV_PCT(100);
    lv_coord_t stacked_min_width = 88;
    bool stacked_flex_grow = false;
};

struct ContentPanelSpec
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
    lv_opa_t bg_opa = LV_OPA_TRANSP;
    std::uint32_t bg_hex = 0;
};

void make_non_scrollable(lv_obj_t* obj);
lv_obj_t* create_root(lv_obj_t* parent, const RootSpec& spec = {});
lv_obj_t* create_header_container(lv_obj_t* parent, const HeaderSpec& spec);
lv_obj_t* create_body(lv_obj_t* parent, const BodySpec& spec = {});
lv_obj_t* create_selector_panel(lv_obj_t* parent, const SelectorPanelSpec& spec = {});
lv_obj_t* create_selector_controls(lv_obj_t* parent, const SelectorControlsSpec& spec = {});
void configure_selector_button(lv_obj_t* button, const SelectorButtonSpec& spec = {});
lv_obj_t* create_content_panel(lv_obj_t* parent, const ContentPanelSpec& spec = {});

} // namespace ui::presentation::directory_browser_layout

#pragma once

#include "lvgl.h"

#include <cstdint>

namespace ui::presentation::service_panel_layout
{

struct RootSpec
{
    lv_coord_t width = LV_PCT(100);
    lv_coord_t height = LV_PCT(100);
    lv_opa_t bg_opa = LV_OPA_COVER;
    std::uint32_t bg_hex = 0xFFF3DF;
    int pad_row = 0;
};

struct HeaderSpec
{
    lv_coord_t height = 0;
};

struct BodySpec
{
    int pad_all = 0;
    int pad_row = 0;
    int pad_column = 0;
};

struct PrimaryPanelSpec
{
    int pad_all = 0;
    int pad_row = 0;
    int pad_column = 0;
    lv_scrollbar_mode_t scrollbar_mode = LV_SCROLLBAR_MODE_OFF;
    lv_opa_t bg_opa = LV_OPA_TRANSP;
    std::uint32_t bg_hex = 0;
};

struct CenterStackSpec
{
    lv_coord_t width = LV_PCT(100);
    int pad_all = 0;
    int pad_row = 0;
};

struct FooterActionsSpec
{
    lv_coord_t height = LV_SIZE_CONTENT;
    int pad_all = 0;
    int pad_row = 0;
    int pad_column = 0;
};

void make_non_scrollable(lv_obj_t* obj);
lv_obj_t* create_root(lv_obj_t* parent, const RootSpec& spec = {});
lv_obj_t* create_header_container(lv_obj_t* parent, const HeaderSpec& spec = {});
lv_obj_t* create_body(lv_obj_t* parent, const BodySpec& spec = {});
lv_obj_t* create_primary_panel(lv_obj_t* parent, const PrimaryPanelSpec& spec = {});
lv_obj_t* create_center_stack(lv_obj_t* parent, const CenterStackSpec& spec = {});
lv_obj_t* create_footer_actions(lv_obj_t* parent, const FooterActionsSpec& spec = {});

} // namespace ui::presentation::service_panel_layout

/**
 * @file page_profile.h
 * @brief Shared business-page layout profiles
 */

#pragma once

#include "lvgl.h"

namespace ui::page_profile
{

enum class LayoutVariant : uint8_t
{
    EncoderCompact = 0,
    HybridCompact,
    HybridTouchLarge,
};

struct PageLayoutProfile
{
    const char* name = "default";
    LayoutVariant variant = LayoutVariant::HybridCompact;

    lv_coord_t top_bar_height = 30;
    lv_coord_t top_content_gap = 3;

    lv_coord_t content_pad_left = 0;
    lv_coord_t content_pad_right = 0;
    lv_coord_t content_pad_top = 0;
    lv_coord_t content_pad_bottom = 0;

    lv_coord_t filter_panel_width = 92;
    lv_coord_t filter_panel_pad_row = 2;
    lv_coord_t filter_button_height = 28;

    lv_coord_t list_panel_pad_row = 6;
    lv_coord_t list_panel_pad_left = 1;
    lv_coord_t list_panel_pad_right = 2;
    lv_coord_t list_panel_margin_bottom = 3;
    lv_coord_t list_item_height = 28;

    lv_coord_t control_button_height = 28;
    lv_coord_t control_button_min_width = 90;
    lv_coord_t compact_button_min_width = 56;

    lv_coord_t modal_min_width = 180;
    lv_coord_t modal_min_height = 132;
    lv_coord_t modal_margin = 12;
    lv_coord_t modal_pad = 8;
    lv_coord_t popup_title_height = 28;
    lv_coord_t icon_picker_button_size = 60;

    lv_coord_t ime_bar_height = 24;
    lv_coord_t ime_toggle_width = 44;
    lv_coord_t ime_toggle_height = 18;
    lv_coord_t ime_candidate_button_height = 28;
    lv_coord_t ime_keyboard_height = 0;

    bool large_touch_hitbox = false;
};

struct ResolvedSize
{
    lv_coord_t width = 0;
    lv_coord_t height = 0;
};

PageLayoutProfile make_pager_profile();
PageLayoutProfile make_tdeck_profile();
PageLayoutProfile make_tab5_profile();
PageLayoutProfile make_default_profile(lv_coord_t width, lv_coord_t height);
const PageLayoutProfile& current();

ResolvedSize resolve_modal_size(lv_coord_t requested_width,
                                lv_coord_t requested_height,
                                lv_obj_t* screen = nullptr);
lv_coord_t resolve_control_button_height();
lv_coord_t resolve_control_button_min_width();
lv_coord_t resolve_compact_button_min_width();
lv_coord_t resolve_modal_pad();
lv_coord_t resolve_popup_title_height();
lv_coord_t resolve_icon_picker_button_size();

} // namespace ui::page_profile

#pragma once

#include "lvgl.h"

namespace ui::menu_profile
{

enum class LayoutVariant : uint8_t
{
    CompactGrid = 0,
    PagerFocus,
    LargeTouchGrid,
};

enum class BadgeAnchorMode : uint8_t
{
    IconTopLeft = 0,
    IconTopRight,
};

enum class InputMode : uint8_t
{
    EncoderPrimary = 0,
    Hybrid,
    TouchPrimary,
};

struct MenuLayoutProfile
{
    const char* name = "default";
    LayoutVariant variant = LayoutVariant::CompactGrid;
    InputMode input_mode = InputMode::Hybrid;
    BadgeAnchorMode badge_anchor_mode = BadgeAnchorMode::IconTopRight;

    lv_coord_t card_width = 150;
    lv_coord_t card_height = 120;
    uint16_t icon_scale = 256;

    lv_coord_t card_radius = 10;
    lv_coord_t card_border_width = 2;
    lv_coord_t card_pad_left = 0;
    lv_coord_t card_pad_right = 0;
    lv_coord_t card_pad_top = 0;
    lv_coord_t card_pad_bottom = 0;
    lv_coord_t card_pad_row = 0;

    lv_coord_t grid_height_pct = 70;
    lv_coord_t grid_top_offset = 30;
    lv_coord_t grid_pad_row = 6;
    lv_coord_t grid_pad_column = 6;
    lv_coord_t grid_pad_left = 0;
    lv_coord_t grid_pad_right = 0;

    lv_coord_t top_bar_height = 30;
    lv_coord_t top_bar_side_inset = 5;
    lv_coord_t top_bar_text_pad = 4;
    lv_coord_t status_row_gap = 2;
    lv_coord_t status_row_offset_y = 2;

    lv_coord_t desc_offset = -10;
    lv_coord_t node_id_offset_x = 5;
    lv_coord_t node_id_offset_y = -10;

    lv_coord_t badge_offset_x = 4;
    lv_coord_t badge_offset_y = -4;
    lv_coord_t badge_min_size = 20;
    lv_coord_t badge_pad_h = 6;
    lv_coord_t badge_pad_v = 2;

    lv_coord_t title_offset_x = 0;
    lv_coord_t title_offset_y = 0;
    lv_coord_t card_label_width_inset = 4;
    uint8_t max_columns = 0;

    bool wrap_grid = false;
    bool vertical_scroll = false;
    bool snap_center = false;
    bool directional_key_nav = false;
    bool transparent_cards = false;
    bool grid_anchor_top_left = false;
    bool show_top_bar = false;
    bool show_card_label = false;
    bool show_desc_label = true;
    bool show_node_id = true;
    bool show_memory_stats = true;
    bool scroll_card_label = false;
    bool large_touch_hitbox = false;

    const lv_font_t* card_label_font = &lv_font_montserrat_14;
    const lv_font_t* desc_font = &lv_font_montserrat_14;
    const lv_font_t* node_id_font = &lv_font_montserrat_14;
    const lv_font_t* top_bar_font = &lv_font_montserrat_14;
    const lv_font_t* badge_font = &lv_font_montserrat_14;
};

const MenuLayoutProfile& current();
MenuLayoutProfile make_tdeck_profile();
MenuLayoutProfile make_pager_profile();
MenuLayoutProfile make_tab5_profile();
MenuLayoutProfile make_default_profile(lv_coord_t width, lv_coord_t height);

} // namespace ui::menu_profile

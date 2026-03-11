#include "ui/menu/menu_profile.h"

#if !defined(LV_FONT_MONTSERRAT_16) || !LV_FONT_MONTSERRAT_16
#define lv_font_montserrat_16 lv_font_montserrat_14
#endif

namespace ui::menu_profile
{
namespace
{

#if !defined(TRAIL_MATE_ESP_BOARD_TAB5) && !defined(ARDUINO_T_LORA_PAGER) && !defined(ARDUINO_T_DECK)
lv_coord_t display_width()
{
    lv_coord_t width = lv_display_get_physical_horizontal_resolution(nullptr);
    return width > 0 ? width : 320;
}

lv_coord_t display_height()
{
    lv_coord_t height = lv_display_get_physical_vertical_resolution(nullptr);
    return height > 0 ? height : 240;
}
#endif

} // namespace

MenuLayoutProfile make_tdeck_profile()
{
    MenuLayoutProfile profile{};
    profile.name = "tdeck";
    profile.variant = LayoutVariant::CompactGrid;
    profile.input_mode = InputMode::Hybrid;
    profile.badge_anchor_mode = BadgeAnchorMode::IconTopRight;
    profile.card_width = 72;
    profile.card_height = 83;
    profile.icon_scale = 176;
    profile.card_radius = 0;
    profile.card_border_width = 0;
    profile.card_pad_top = 4;
    profile.card_pad_bottom = 4;
    profile.card_pad_row = 2;
    profile.grid_height_pct = 80;
    profile.grid_top_offset = 30;
    profile.grid_pad_row = 6;
    profile.grid_pad_column = 0;
    profile.top_bar_height = 30;
    profile.top_bar_side_inset = 5;
    profile.top_bar_text_pad = 4;
    profile.status_row_gap = 2;
    profile.status_row_offset_y = 2;
    profile.desc_offset = -10;
    profile.node_id_offset_x = 4;
    profile.node_id_offset_y = -4;
    profile.badge_offset_x = 4;
    profile.badge_offset_y = -4;
    profile.badge_min_size = 20;
    profile.badge_pad_h = 6;
    profile.badge_pad_v = 2;
    profile.title_offset_x = 0;
    profile.title_offset_y = 0;
    profile.wrap_grid = true;
    profile.vertical_scroll = true;
    profile.snap_center = false;
    profile.transparent_cards = true;
    profile.show_top_bar = false;
    profile.show_card_label = true;
    profile.show_desc_label = false;
    profile.show_node_id = true;
    profile.large_touch_hitbox = false;
    profile.card_label_font = &lv_font_montserrat_12;
    profile.desc_font = &lv_font_montserrat_14;
    profile.node_id_font = &lv_font_montserrat_14;
    profile.top_bar_font = &lv_font_montserrat_14;
    profile.badge_font = &lv_font_montserrat_14;
    return profile;
}

MenuLayoutProfile make_pager_profile()
{
    MenuLayoutProfile profile{};
    profile.name = "pager";
    profile.variant = LayoutVariant::PagerFocus;
    profile.input_mode = InputMode::EncoderPrimary;
    profile.badge_anchor_mode = BadgeAnchorMode::IconTopLeft;
    profile.card_width = 150;
    profile.card_height = 122;
    profile.icon_scale = 256;
    profile.card_radius = 10;
    profile.card_border_width = 2;
    profile.grid_height_pct = 55;
    profile.grid_top_offset = 46;
    profile.grid_pad_row = 0;
    profile.grid_pad_column = 10;
    profile.grid_pad_left = 5;
    profile.grid_pad_right = 5;
    profile.top_bar_height = 30;
    profile.top_bar_side_inset = 5;
    profile.top_bar_text_pad = 4;
    profile.status_row_gap = 2;
    profile.status_row_offset_y = 2;
    profile.desc_offset = -10;
    profile.node_id_offset_x = 5;
    profile.node_id_offset_y = -10;
    profile.badge_offset_x = -4;
    profile.badge_offset_y = -4;
    profile.badge_min_size = 20;
    profile.badge_pad_h = 6;
    profile.badge_pad_v = 2;
    profile.title_offset_x = 0;
    profile.title_offset_y = 0;
    profile.wrap_grid = false;
    profile.vertical_scroll = false;
    profile.snap_center = true;
    profile.transparent_cards = false;
    profile.show_top_bar = false;
    profile.show_card_label = false;
    profile.show_desc_label = true;
    profile.show_node_id = true;
    profile.large_touch_hitbox = false;
    profile.card_label_font = &lv_font_montserrat_14;
    profile.desc_font = &lv_font_montserrat_14;
    profile.node_id_font = &lv_font_montserrat_14;
    profile.top_bar_font = &lv_font_montserrat_14;
    profile.badge_font = &lv_font_montserrat_14;
    return profile;
}

MenuLayoutProfile make_tab5_profile()
{
    MenuLayoutProfile profile = make_tdeck_profile();
    profile.name = "tab5";
    profile.variant = LayoutVariant::LargeTouchGrid;
    profile.input_mode = InputMode::TouchPrimary;
    profile.badge_anchor_mode = BadgeAnchorMode::IconTopRight;
    profile.card_width = 176;
    profile.card_height = 150;
    profile.icon_scale = 256;
    profile.card_radius = 0;
    profile.card_border_width = 0;
    profile.card_pad_top = 10;
    profile.card_pad_bottom = 10;
    profile.card_pad_row = 6;
    profile.grid_height_pct = 80;
    profile.grid_top_offset = 46;
    profile.grid_pad_row = 10;
    profile.grid_pad_column = 10;
    profile.top_bar_height = 46;
    profile.top_bar_side_inset = 10;
    profile.top_bar_text_pad = 8;
    profile.status_row_gap = 4;
    profile.status_row_offset_y = 6;
    profile.desc_offset = -12;
    profile.node_id_offset_x = 10;
    profile.node_id_offset_y = -6;
    profile.badge_offset_x = 2;
    profile.badge_offset_y = -4;
    profile.badge_min_size = 22;
    profile.badge_pad_h = 6;
    profile.badge_pad_v = 3;
    profile.title_offset_x = 0;
    profile.title_offset_y = 0;
    profile.wrap_grid = true;
    profile.vertical_scroll = true;
    profile.snap_center = false;
    profile.transparent_cards = true;
    profile.show_top_bar = true;
    profile.show_card_label = true;
    profile.show_desc_label = false;
    profile.show_node_id = true;
    profile.large_touch_hitbox = true;
    profile.card_label_font = &lv_font_montserrat_16;
    profile.desc_font = &lv_font_montserrat_16;
    profile.node_id_font = &lv_font_montserrat_16;
    profile.top_bar_font = &lv_font_montserrat_16;
    profile.badge_font = &lv_font_montserrat_16;
    return profile;
}

MenuLayoutProfile make_default_profile(lv_coord_t width, lv_coord_t height)
{
    if (width >= 700 || height >= 700)
    {
        return make_tab5_profile();
    }
    if (width >= 400)
    {
        return make_pager_profile();
    }
    return make_tdeck_profile();
}

const MenuLayoutProfile& current()
{
    static MenuLayoutProfile profile = []()
    {
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
        return make_tab5_profile();
#elif defined(ARDUINO_T_LORA_PAGER)
        return make_pager_profile();
#elif defined(ARDUINO_T_DECK)
        return make_tdeck_profile();
#else
        return make_default_profile(display_width(), display_height());
#endif
    }();
    return profile;
}

} // namespace ui::menu_profile

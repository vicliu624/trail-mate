#include "ui/menu/menu_profile.h"

#include <cassert>
#include <cstring>

const lv_font_t lv_font_montserrat_10{};
const lv_font_t lv_font_montserrat_12{};
const lv_font_t lv_font_montserrat_14{};
const lv_font_t lv_font_montserrat_16{};

lv_coord_t lv_display_get_physical_horizontal_resolution(lv_display_t*)
{
    return 320;
}

lv_coord_t lv_display_get_physical_vertical_resolution(lv_display_t*)
{
    return 170;
}

int main()
{
    const auto& profile = ui::menu_profile::current();

    assert(std::strcmp(profile.name, "cardputer_zero") == 0);
    const auto pager = ui::menu_profile::make_pager_profile();
    assert(profile.variant == pager.variant);
    assert(profile.variant == ui::menu_profile::LayoutVariant::CompactGrid);
    assert(profile.badge_anchor_mode == pager.badge_anchor_mode);
    assert(!profile.snap_center);
    assert(profile.vertical_scroll);
    assert(profile.wrap_grid);
    assert(profile.transparent_cards);

    assert(profile.input_mode == ui::menu_profile::InputMode::Hybrid);
    assert(profile.directional_key_nav);
    assert(profile.show_top_bar);
    assert(!profile.show_memory_stats);
    assert(profile.show_card_label);
    assert(!profile.show_desc_label);
    assert(profile.show_node_id);

    // Four columns and two complete rows fit between the fixed status bars.
    const int columns = (320 - profile.grid_pad_left - profile.grid_pad_right + profile.grid_pad_column) /
                        (profile.card_width + profile.grid_pad_column);
    assert(columns == 4);
    const int available_height = 170 - 2 * profile.top_bar_height;
    assert(2 * profile.card_height + profile.grid_pad_row <= available_height);
    assert(3 * profile.card_height + 2 * profile.grid_pad_row > available_height);
    assert(profile.icon_scale == 128);
    assert(profile.card_label_font == &lv_font_montserrat_10);
    assert(profile.grid_top_offset == 26);
    assert(profile.top_bar_height == 24);
    return 0;
}

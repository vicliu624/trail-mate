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
    const auto pager = ui::menu_profile::make_pager_profile();
    const auto& profile = ui::menu_profile::current();

    assert(std::strcmp(profile.name, "cardputer_zero") == 0);
    assert(profile.variant == ui::menu_profile::LayoutVariant::PagerFocus);
    assert(profile.badge_anchor_mode == pager.badge_anchor_mode);
    assert(profile.snap_center == pager.snap_center);
    assert(!profile.vertical_scroll);
    assert(!profile.wrap_grid);

    assert(profile.input_mode == ui::menu_profile::InputMode::Hybrid);
    assert(profile.directional_key_nav);
    assert(profile.show_top_bar);
    assert(!profile.show_memory_stats);
    assert(!profile.show_card_label);
    assert(profile.show_desc_label);
    assert(profile.show_node_id);

    assert(profile.card_width == 112);
    assert(profile.card_height == 80);
    assert(profile.icon_scale == 208);
    assert(profile.grid_height_pct == 50);
    assert(profile.grid_top_offset == 26);
    assert(profile.top_bar_height == 24);
    return 0;
}

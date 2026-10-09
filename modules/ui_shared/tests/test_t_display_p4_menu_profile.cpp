#include "ui/menu/menu_profile.h"

#include <cassert>
#include <cstring>

const lv_font_t lv_font_montserrat_10{};
const lv_font_t lv_font_montserrat_12{};
const lv_font_t lv_font_montserrat_14{};
const lv_font_t lv_font_montserrat_16{};

lv_coord_t lv_display_get_physical_horizontal_resolution(lv_display_t*)
{
    return 720;
}

lv_coord_t lv_display_get_physical_vertical_resolution(lv_display_t*)
{
    return 720;
}

int main()
{
    const auto& profile = ui::menu_profile::current();
    assert(std::strcmp(profile.name, "t_display_p4") == 0);
    assert(profile.variant == ui::menu_profile::LayoutVariant::LargeTouchGrid);
    assert(profile.input_mode == ui::menu_profile::InputMode::TouchPrimary);
    assert(profile.large_touch_hitbox);
    assert(profile.show_top_bar);
    assert(profile.max_columns == 4);
    assert(profile.card_width == 104);
    assert(profile.card_height == 112);
    assert(profile.grid_top_offset == 34);
    return 0;
}

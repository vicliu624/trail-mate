#include "ui/page/page_profile.h"

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

lv_coord_t lv_obj_get_width(lv_obj_t*)
{
    return 320;
}

lv_coord_t lv_obj_get_height(lv_obj_t*)
{
    return 170;
}

lv_obj_t* lv_screen_active()
{
    return nullptr;
}

int main()
{
    const auto pager = ui::page_profile::make_pager_profile();
    assert(std::strcmp(pager.name, "pager") == 0);
    assert(!pager.dense);
    assert(pager.top_bar_height == 24);
    assert(pager.filter_panel_width == 90);
    assert(pager.filter_button_height == 28);
    assert(pager.list_item_height == 28);
    assert(pager.control_button_height == 28);

    const auto zero = ui::page_profile::make_cardputer_zero_profile();
    assert(std::strcmp(zero.name, "cardputer_zero") == 0);
    assert(zero.dense);
    assert(zero.top_bar_height == 24);
    assert(zero.filter_panel_width == 78);
    assert(zero.filter_button_height == 24);
    assert(zero.list_item_height == 24);
    assert(zero.control_button_height == 24);
    assert(zero.title_font == &lv_font_montserrat_12);
    assert(zero.body_font == &lv_font_montserrat_12);
    assert(zero.caption_font == &lv_font_montserrat_10);
    assert(zero.tiny_font == &lv_font_montserrat_10);
    assert(zero.filter_panel_width < pager.filter_panel_width);
    assert(zero.list_item_height < pager.list_item_height);

    const auto& current = ui::page_profile::current();
    assert(std::strcmp(current.name, "cardputer_zero") == 0);
    assert(ui::page_profile::is_dense());
    assert(current.filter_panel_width == zero.filter_panel_width);
    assert(ui::page_profile::resolve_title_font() == &lv_font_montserrat_12);
    assert(ui::page_profile::resolve_caption_font() == &lv_font_montserrat_10);
    return 0;
}

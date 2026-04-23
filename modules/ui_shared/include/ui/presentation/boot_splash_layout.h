#pragma once

#include "lvgl.h"

#include <cstdint>

namespace ui::presentation::boot_splash_layout
{

struct RootSpec
{
    lv_coord_t width = LV_PCT(100);
    lv_coord_t height = LV_PCT(100);
    lv_opa_t bg_opa = LV_OPA_COVER;
    std::uint32_t bg_hex = 0;
};

struct HeroSpec
{
    lv_coord_t offset_x = 0;
    lv_coord_t offset_y = 0;
};

struct LogSpec
{
    lv_coord_t width = LV_PCT(100);
    lv_coord_t inset_x = 10;
    lv_coord_t bottom_inset = 8;
    lv_color_t text_color = lv_color_hex(0);
    const lv_font_t* font = nullptr;
};

void make_non_scrollable(lv_obj_t* obj);
lv_obj_t* create_root(lv_obj_t* parent, const RootSpec& spec = {});
void align_hero(lv_obj_t* hero, const HeroSpec& spec = {});
lv_obj_t* create_log_label(lv_obj_t* parent, const LogSpec& spec = {});

} // namespace ui::presentation::boot_splash_layout

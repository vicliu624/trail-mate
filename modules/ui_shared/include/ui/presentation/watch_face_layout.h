#pragma once

#include "lvgl.h"

#include <cstdint>

namespace ui::presentation::watch_face_layout
{

struct RootSpec
{
    lv_coord_t width = LV_PCT(100);
    lv_coord_t height = LV_PCT(100);
    lv_opa_t bg_opa = LV_OPA_COVER;
    std::uint32_t bg_hex = 0;
};

struct PrimaryRegionSpec
{
    std::int16_t width_pct = 100;
    std::int16_t height_pct = 100;
    lv_coord_t offset_x = 0;
    lv_coord_t offset_y = 0;
};

void make_non_scrollable(lv_obj_t* obj);
lv_obj_t* create_root(lv_obj_t* parent, const RootSpec& spec = {});
lv_obj_t* create_primary_region(lv_obj_t* parent, const PrimaryRegionSpec& spec = {});

} // namespace ui::presentation::watch_face_layout

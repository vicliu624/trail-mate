#pragma once

#include "lvgl.h"

namespace ui::components::info_card
{

struct ContentOptions
{
    bool header_meta = false;
    bool body_meta = false;
};

struct ContentSlots
{
    lv_obj_t* header_row = nullptr;
    lv_obj_t* header_main_label = nullptr;
    lv_obj_t* header_meta_label = nullptr;
    lv_obj_t* body_row = nullptr;
    lv_obj_t* body_main_label = nullptr;
    lv_obj_t* body_meta_label = nullptr;
};

bool use_tdeck_layout();
lv_coord_t resolve_height(lv_coord_t base_height);
void configure_item(lv_obj_t* obj, lv_coord_t base_height);
void apply_item_style(lv_obj_t* obj);
void apply_single_line_label(lv_obj_t* label);
ContentSlots create_content(lv_obj_t* parent, const ContentOptions& options = {});

} // namespace ui::components::info_card

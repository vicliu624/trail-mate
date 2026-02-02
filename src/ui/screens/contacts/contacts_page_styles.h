#pragma once
#include "lvgl.h"

namespace contacts::ui::style
{

// Must be called before applying any styles (safe to call repeatedly)
void init_once();

/* Panels */
void apply_panel_side(lv_obj_t* obj);      // filter_panel / action_panel (gray)
void apply_panel_main(lv_obj_t* obj);      // list_panel (white)
void apply_container_white(lv_obj_t* obj); // sub_container / bottom_container (white)

/* Buttons */
void apply_btn_basic(lv_obj_t* btn);  // common button style
void apply_btn_filter(lv_obj_t* btn); // filter buttons (same as basic, but supports CHECKED state highlight)

/* List item */
void apply_list_item(lv_obj_t* item); // item base + focused behavior

/* Labels */
void apply_label_primary(lv_obj_t* label); // name
void apply_label_muted(lv_obj_t* label);   // status/snr

} // namespace contacts::ui::style

/**
 * @file settings_page_styles.h
 * @brief Settings styles
 */

#pragma once

#include "lvgl.h"

namespace settings::ui::style
{

void init_once();

void apply_panel_side(lv_obj_t* obj);
void apply_panel_main(lv_obj_t* obj);
void apply_btn_basic(lv_obj_t* btn);
void apply_btn_filter(lv_obj_t* btn);
void apply_list_item(lv_obj_t* item);
void apply_modal_bg(lv_obj_t* obj);
void apply_modal_panel(lv_obj_t* obj);
void apply_btn_modal(lv_obj_t* btn);

void apply_label_primary(lv_obj_t* label);
void apply_label_muted(lv_obj_t* label);

} // namespace settings::ui::style

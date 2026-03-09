/**
 * @file team_page_styles.h
 * @brief Team page styles
 */

#pragma once

#include "lvgl.h"

namespace team
{
namespace ui
{
namespace style
{

void init_once();

void apply_root(lv_obj_t* obj);
void apply_header(lv_obj_t* obj);
void apply_content(lv_obj_t* obj);
void apply_body(lv_obj_t* obj);
void apply_actions(lv_obj_t* obj);
void apply_section_label(lv_obj_t* obj);
void apply_meta_label(lv_obj_t* obj);
void apply_list_item(lv_obj_t* obj);
void apply_button_primary(lv_obj_t* obj);
void apply_button_secondary(lv_obj_t* obj);

} // namespace style
} // namespace ui
} // namespace team

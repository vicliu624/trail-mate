/**
 * @file settings_page_layout.h
 * @brief Settings layout
 */

#pragma once

#include "lvgl.h"

namespace settings::ui::layout
{

lv_obj_t* create_root(lv_obj_t* parent);
lv_obj_t* create_header(lv_obj_t* root, void (*back_callback)(void*), void* user_data);
lv_obj_t* create_content(lv_obj_t* root);
void create_filter_panel(lv_obj_t* parent);
void create_list_panel(lv_obj_t* parent);

} // namespace settings::ui::layout

/**
 * @file team_page_layout.h
 * @brief Team page layout (structure only)
 */

#pragma once

#include "lvgl.h"

namespace team
{
namespace ui
{
namespace layout
{

lv_obj_t* create_root(lv_obj_t* parent);
lv_obj_t* create_header(lv_obj_t* root);
lv_obj_t* create_content(lv_obj_t* root);
lv_obj_t* create_state_container(lv_obj_t* content);
lv_obj_t* create_idle_container(lv_obj_t* parent);
lv_obj_t* create_active_container(lv_obj_t* parent);

} // namespace layout
} // namespace ui
} // namespace team

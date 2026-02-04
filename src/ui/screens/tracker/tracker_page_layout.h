#pragma once

#include "lvgl.h"

namespace tracker
{
namespace ui
{
namespace layout
{

lv_obj_t* create_root(lv_obj_t* parent);
lv_obj_t* create_header(lv_obj_t* root);
lv_obj_t* create_content(lv_obj_t* root);
lv_obj_t* create_filter_panel(lv_obj_t* content, int width);
lv_obj_t* create_list_panel(lv_obj_t* content);
lv_obj_t* create_action_panel(lv_obj_t* content, int width);
lv_obj_t* create_list_container(lv_obj_t* list_panel);
lv_obj_t* create_bottom_bar(lv_obj_t* list_panel);

} // namespace layout
} // namespace ui
} // namespace tracker

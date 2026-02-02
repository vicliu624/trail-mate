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
lv_obj_t* create_start_stop_button(lv_obj_t* content);
lv_obj_t* create_list(lv_obj_t* content);

} // namespace layout
} // namespace ui
} // namespace tracker

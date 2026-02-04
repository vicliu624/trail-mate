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
lv_obj_t* create_body(lv_obj_t* root);
lv_obj_t* create_mode_panel(lv_obj_t* body, int width);
lv_obj_t* create_main_panel(lv_obj_t* body);
lv_obj_t* create_section(lv_obj_t* parent);

} // namespace layout
} // namespace ui
} // namespace tracker

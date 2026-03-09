#pragma once

#include "lvgl.h"

namespace tracker
{
namespace ui
{
namespace components
{

void init_page(lv_obj_t* parent);
void cleanup_page();
void refresh_page();

} // namespace components
} // namespace ui
} // namespace tracker

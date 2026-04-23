#pragma once

#include "lvgl.h"

namespace tracker
{
namespace ui
{
namespace input
{

void init_tracker_input();
void cleanup_tracker_input();
void tracker_input_on_ui_refreshed();
void tracker_focus_to_selector();
void tracker_focus_to_content();
lv_group_t* tracker_input_get_group();

} // namespace input
} // namespace ui
} // namespace tracker

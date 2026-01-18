/**
 * @file settings_page_input.h
 * @brief Settings input handling
 */

#pragma once

#include "lvgl.h"

namespace settings::ui::input {

void init();
void cleanup();
void on_ui_refreshed();
void focus_to_filter();
void focus_to_list();
lv_group_t* get_group();

} // namespace settings::ui::input

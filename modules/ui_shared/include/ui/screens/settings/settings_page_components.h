/**
 * @file settings_page_components.h
 * @brief Settings UI components (public interface)
 */

#pragma once

#include "lvgl.h"

namespace settings::ui::components
{

void create(lv_obj_t* parent);
void destroy();
bool activate_filter_button(lv_obj_t* filter_button);
bool activate_list_button(lv_obj_t* list_button);
bool activate_list_back_button(lv_obj_t* list_back_button);

} // namespace settings::ui::components

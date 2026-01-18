/**
 * @file settings_page_components.h
 * @brief Settings UI components (public interface)
 */

#pragma once

#include "lvgl.h"

namespace settings::ui::components {

void create(lv_obj_t* parent);
void destroy();

} // namespace settings::ui::components

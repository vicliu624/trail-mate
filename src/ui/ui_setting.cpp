/**
 * @file ui_setting.cpp
 * @brief Settings entry points (thin wrapper)
 */

#include "screens/settings/settings_page_components.h"

void ui_setting_enter(lv_obj_t* parent)
{
    settings::ui::components::create(parent);
}

void ui_setting_exit(lv_obj_t* parent)
{
    (void)parent;
    settings::ui::components::destroy();
}

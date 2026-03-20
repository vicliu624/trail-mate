#pragma once

#include "lvgl.h"

#include "ui/menu/menu_layout.h"

namespace ui::menu::dashboard
{

#if defined(TRAIL_MATE_ESP_BOARD_TAB5) || defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
void init(lv_obj_t* menu_panel, lv_obj_t* grid_panel, const ui::menu_layout::InitOptions& options);
void bringToFront();
void setActive(bool active);
#else
inline void init(lv_obj_t* menu_panel, lv_obj_t* grid_panel, const ui::menu_layout::InitOptions& options)
{
    (void)menu_panel;
    (void)grid_panel;
    (void)options;
}

inline void bringToFront()
{
}

inline void setActive(bool active)
{
    (void)active;
}
#endif

} // namespace ui::menu::dashboard

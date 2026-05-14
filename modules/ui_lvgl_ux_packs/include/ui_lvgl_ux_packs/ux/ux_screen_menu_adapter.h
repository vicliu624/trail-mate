#pragma once

#include "ui_lvgl_ux_packs/ux/screen_registry.h"
#include "ui_lvgl_ux_packs/ux/ux_menu_model.h"

namespace ui_lvgl_ux
{

class UxScreenMenuAdapter
{
  public:
    void buildMenu(const ScreenRegistry& screens, UxMenuModel& out) const;
};

} // namespace ui_lvgl_ux

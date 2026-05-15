#pragma once

#include "ui_presentation/menu/menu_model.h"
#include "ui_lvgl_ux_packs/ux/screen_registry.h"

namespace ui_lvgl_ux
{

class UxScreenMenuAdapter
{
  public:
    void buildMenu(const ScreenRegistry& screens, ui::menu::MenuModel& out) const;
};

} // namespace ui_lvgl_ux

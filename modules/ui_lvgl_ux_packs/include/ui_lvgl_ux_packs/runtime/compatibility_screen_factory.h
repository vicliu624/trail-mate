#pragma once

#include "ui_presentation/menu/menu_model.h"
#include "ui_presentation/screen/screen_binding_registry.h"

namespace ui_lvgl_ux
{

struct CompatibilityScreenDescriptor
{
    ui::menu::MenuScreenId screen_id = ui::menu::MenuScreenId::Dashboard;
    const char* binding_id = nullptr;
    bool available = false;
};

class CompatibilityScreenFactory
{
  public:
    bool describe(ui::menu::MenuScreenId screen_id,
                  CompatibilityScreenDescriptor& out) const;
    void buildBindingsForMenu(const ui::menu::MenuModel& menu,
                              ui::screen::ScreenBindingRegistry& out) const;
};

} // namespace ui_lvgl_ux

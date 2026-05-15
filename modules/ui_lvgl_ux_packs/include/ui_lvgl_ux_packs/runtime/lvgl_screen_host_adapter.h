#pragma once

#include "ui_presentation/menu/menu_model.h"

namespace ui_lvgl_ux
{

struct LvglScreenHostEntry
{
    ui::menu::MenuScreenId screen_id = ui::menu::MenuScreenId::Dashboard;
    const char* binding_id = nullptr;
    bool available = false;
};

class LvglScreenHostAdapter
{
  public:
    bool resolve(ui::menu::MenuScreenId screen_id,
                 LvglScreenHostEntry& out) const;
};

} // namespace ui_lvgl_ux

#pragma once

#include "ui_presentation/menu/menu_model.h"
#include "ui_presentation/screen/screen_route.h"

#include <cstddef>

namespace ui_lvgl_ux
{

struct LvglMenuEntry
{
    ui::menu::MenuScreenId screen_id = ui::menu::MenuScreenId::Dashboard;
    ui::screen::ScreenRoute route{};
    const char* label = nullptr;
    bool enabled = false;
};

class LvglMenuRuntimeAdapter
{
  public:
    bool buildEntries(const ui::menu::MenuModel& menu,
                      LvglMenuEntry* out,
                      std::size_t capacity,
                      std::size_t& out_count) const;
};

} // namespace ui_lvgl_ux

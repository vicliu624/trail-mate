#pragma once

#include "ui_presentation/menu/menu_model.h"
#include "ui_presentation/screen/screen_route.h"

#include <cstddef>

namespace trailmate::uconsole
{

struct GtkMenuDescriptor
{
    ui::menu::MenuScreenId screen_id = ui::menu::MenuScreenId::Dashboard;
    ui::screen::ScreenRoute route{};
    const char* label = nullptr;
    bool enabled = false;
};

class GtkMenuRuntimeAdapter
{
  public:
    bool buildDescriptors(const ui::menu::MenuModel& menu,
                          GtkMenuDescriptor* out,
                          std::size_t capacity,
                          std::size_t& out_count) const;
};

} // namespace trailmate::uconsole

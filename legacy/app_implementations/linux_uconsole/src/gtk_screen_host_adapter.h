#pragma once

#include "ui_presentation/screen/screen_route.h"

namespace trailmate::uconsole
{

struct GtkScreenDescriptor
{
    ui::menu::MenuScreenId screen_id = ui::menu::MenuScreenId::Dashboard;
    const char* binding_id = nullptr;
    bool available = false;
};

class GtkScreenHostAdapter
{
  public:
    bool resolve(const ui::screen::ScreenRoute& route,
                 GtkScreenDescriptor& out) const;
};

} // namespace trailmate::uconsole

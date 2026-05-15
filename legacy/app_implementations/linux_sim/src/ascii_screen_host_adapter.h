#pragma once

#include "ui_presentation/screen/screen_route.h"

namespace trailmate::linux_sim
{

struct AsciiScreenDescriptor
{
    ui::menu::MenuScreenId screen_id = ui::menu::MenuScreenId::Dashboard;
    const char* binding_id = nullptr;
    bool available = false;
};

class AsciiScreenHostAdapter
{
  public:
    bool resolve(const ui::screen::ScreenRoute& route,
                 AsciiScreenDescriptor& out) const;
};

} // namespace trailmate::linux_sim

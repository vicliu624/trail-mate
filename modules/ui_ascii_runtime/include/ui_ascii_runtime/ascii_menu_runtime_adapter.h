#pragma once

#include "ui_presentation/menu/menu_model.h"
#include "ui_presentation/screen/screen_route.h"

#include <cstddef>

namespace trailmate::linux_sim
{

struct AsciiMenuLine
{
    ui::menu::MenuScreenId screen_id = ui::menu::MenuScreenId::Dashboard;
    ui::screen::ScreenRoute route{};
    const char* label = nullptr;
    bool enabled = false;
};

class AsciiMenuRuntimeAdapter
{
  public:
    bool buildLines(const ui::menu::MenuModel& menu,
                    AsciiMenuLine* out,
                    std::size_t capacity,
                    std::size_t& out_count) const;
};

} // namespace trailmate::linux_sim

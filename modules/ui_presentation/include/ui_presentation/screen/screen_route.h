#pragma once

#include "ui_presentation/menu/menu_model.h"

#include <cstdint>

namespace ui
{
namespace screen
{

enum class ScreenOpenMode : uint8_t
{
    Replace,
    Push,
    Modal,
};

struct ScreenRoute
{
    ui::menu::MenuScreenId screen_id = ui::menu::MenuScreenId::Dashboard;
    ScreenOpenMode open_mode = ScreenOpenMode::Replace;
    bool valid = false;
};

inline ScreenRoute routeForMenuScreen(ui::menu::MenuScreenId id)
{
    ScreenRoute route;
    route.screen_id = id;
    route.open_mode = ScreenOpenMode::Replace;
    route.valid = true;
    return route;
}

} // namespace screen
} // namespace ui

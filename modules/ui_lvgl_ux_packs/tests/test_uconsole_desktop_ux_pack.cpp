#include "ui_lvgl_ux_packs/packs/uconsole_desktop_ux_pack.h"

#include <cassert>
#include <cstring>

int main()
{
    ui_lvgl_ux::UConsoleDesktopUxPack pack;
    assert(std::strcmp(pack.id(), "uconsole_desktop") == 0);
    assert(pack.profile().screen_class == ui_lvgl_ux::ScreenClass::Desktop);
    assert(pack.profile().input_model ==
           ui_lvgl_ux::InputModel::DesktopKeyboardMouse);
    assert(pack.profile().map_mode == ui_lvgl_ux::MapMode::Desktop);
    assert(pack.profile().chat_mode == ui_lvgl_ux::ChatMode::Full);
    assert(pack.features().chat);
    assert(!pack.features().contacts);
    assert(!pack.features().walkie);

    ui_lvgl_ux::ScreenRegistry screens;
    pack.buildScreens(screens);
    assert(screens.size() == 7);
    assert(screens.items()[0].id == ui_lvgl_ux::ScreenId::Dashboard);
    assert(screens.items()[1].id == ui_lvgl_ux::ScreenId::Chat);
    assert(screens.items()[6].id == ui_lvgl_ux::ScreenId::Settings);
    return 0;
}

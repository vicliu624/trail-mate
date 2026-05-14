#include "ui_lvgl_ux_packs/packs/simulator_full_ux_pack.h"

#include <cassert>
#include <cstring>

int main()
{
    ui_lvgl_ux::SimulatorFullUxPack pack;
    assert(std::strcmp(pack.id(), "simulator_full") == 0);
    assert(pack.profile().screen_class == ui_lvgl_ux::ScreenClass::Desktop);
    assert(pack.profile().input_model ==
           ui_lvgl_ux::InputModel::DesktopKeyboardMouse);
    assert(pack.profile().map_mode == ui_lvgl_ux::MapMode::Full);
    assert(pack.profile().chat_mode == ui_lvgl_ux::ChatMode::Full);
    assert(pack.features().contacts);
    assert(pack.features().walkie);
    assert(pack.features().sstv);
    assert(pack.features().extensions);

    ui_lvgl_ux::ScreenRegistry screens;
    pack.buildScreens(screens);
    assert(screens.size() == 11);
    assert(screens.items()[10].id == ui_lvgl_ux::ScreenId::Extensions);
    return 0;
}

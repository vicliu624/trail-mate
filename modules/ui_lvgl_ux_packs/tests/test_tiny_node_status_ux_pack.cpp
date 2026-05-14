#include "ui_lvgl_ux_packs/packs/tiny_node_status_ux_pack.h"

#include <cassert>
#include <cstring>

int main()
{
    ui_lvgl_ux::TinyNodeStatusUxPack pack;
    assert(std::strcmp(pack.id(), "tiny_node_status") == 0);
    assert(pack.profile().screen_class == ui_lvgl_ux::ScreenClass::TinyStatus);
    assert(pack.profile().input_model == ui_lvgl_ux::InputModel::ButtonsOnly);
    assert(pack.profile().map_mode == ui_lvgl_ux::MapMode::None);
    assert(pack.profile().chat_mode == ui_lvgl_ux::ChatMode::StatusOnly);
    assert(!pack.features().chat);
    assert(pack.features().team);
    assert(pack.features().gps);

    ui_lvgl_ux::ScreenRegistry screens;
    pack.buildScreens(screens);
    assert(screens.size() == 4);
    assert(screens.items()[0].id == ui_lvgl_ux::ScreenId::Dashboard);
    assert(screens.items()[1].id == ui_lvgl_ux::ScreenId::Team);
    return 0;
}

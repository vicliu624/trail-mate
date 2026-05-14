#include "ui_lvgl_ux_packs/packs/compatibility_ux_pack.h"

#include <cassert>
#include <cstring>

int main()
{
    ui_lvgl_ux::CompatibilityUxPack pack;
    assert(std::strcmp(pack.id(), "compatibility") == 0);
    assert(pack.profile().screen_class == ui_lvgl_ux::ScreenClass::CompactHandheld);
    assert(pack.profile().input_model == ui_lvgl_ux::InputModel::ButtonsOnly);
    assert(pack.features().chat);
    assert(pack.features().walkie);
    assert(pack.features().sstv);
    assert(pack.features().extensions);

    ui_lvgl_ux::ScreenRegistry screens;
    pack.buildScreens(screens);
    assert(screens.size() == 11);
    assert(screens.items()[0].id == ui_lvgl_ux::ScreenId::Dashboard);
    assert(screens.items()[1].id == ui_lvgl_ux::ScreenId::Chat);
    assert(screens.items()[10].id == ui_lvgl_ux::ScreenId::Extensions);

    ui_lvgl_ux::InputBindingSet bindings;
    pack.buildInputBindings(bindings);
    assert(bindings.size() >= 8);
    return 0;
}

#include "ui_lvgl_ux_packs/packs/cardputer_compact_ux_pack.h"

#include <cassert>
#include <cstring>

int main()
{
    ui_lvgl_ux::CardputerCompactUxPack pack;
    assert(std::strcmp(pack.id(), "cardputer_compact") == 0);
    assert(pack.profile().screen_class == ui_lvgl_ux::ScreenClass::CompactHandheld);
    assert(pack.profile().input_model == ui_lvgl_ux::InputModel::Keyboard);
    assert(pack.profile().map_mode == ui_lvgl_ux::MapMode::Compact);
    assert(pack.profile().chat_mode == ui_lvgl_ux::ChatMode::QuickMessage);
    assert(pack.features().chat);
    assert(pack.features().contacts);
    assert(pack.features().map);
    assert(pack.features().gps);
    assert(pack.features().settings);
    assert(pack.features().walkie);
    assert(!pack.features().sstv);
    assert(pack.features().extensions);

    ui_lvgl_ux::ScreenRegistry screens;
    pack.buildScreens(screens);
    assert(screens.size() == 14);
    assert(screens.items()[7].id == ui_lvgl_ux::ScreenId::Calculator);
    assert(screens.items()[8].id == ui_lvgl_ux::ScreenId::Geocaching);
    assert(screens.items()[9].id == ui_lvgl_ux::ScreenId::EnergySweep);
    assert(screens.items()[3].id == ui_lvgl_ux::ScreenId::Map);
    assert(screens.items()[4].id == ui_lvgl_ux::ScreenId::SkyPlot);
    for (std::size_t index = 0; index < screens.size(); ++index)
    {
        assert(screens.items()[index].id != ui_lvgl_ux::ScreenId::Gps);
        assert(screens.items()[index].id != ui_lvgl_ux::ScreenId::Sstv);
    }

    ui_lvgl_ux::InputBindingSet bindings;
    pack.buildInputBindings(bindings);
    assert(bindings.size() == 10);
    return 0;
}

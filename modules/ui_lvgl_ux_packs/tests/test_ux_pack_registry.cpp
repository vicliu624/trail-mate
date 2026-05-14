#include "ui_lvgl_ux_packs/ux/ux_pack_registry.h"

#include <cassert>
#include <cstring>

int main()
{
    const ui_lvgl_ux::IUxPack* compatibility =
        ui_lvgl_ux::findUxPackById("compatibility");
    const ui_lvgl_ux::IUxPack* uconsole =
        ui_lvgl_ux::findUxPackById("uconsole_desktop");
    const ui_lvgl_ux::IUxPack* tiny =
        ui_lvgl_ux::findUxPackById("tiny_node_status");
    const ui_lvgl_ux::IUxPack* simulator =
        ui_lvgl_ux::findUxPackById("simulator_full");

    assert(compatibility != nullptr);
    assert(uconsole != nullptr);
    assert(tiny != nullptr);
    assert(simulator != nullptr);
    assert(std::strcmp(compatibility->id(), "compatibility") == 0);
    assert(std::strcmp(uconsole->id(), "uconsole_desktop") == 0);
    assert(std::strcmp(tiny->id(), "tiny_node_status") == 0);
    assert(std::strcmp(simulator->id(), "simulator_full") == 0);
    assert(ui_lvgl_ux::findUxPackById("missing") == nullptr);
    assert(ui_lvgl_ux::findUxPackById(nullptr) == nullptr);

    ui_lvgl_ux::ScreenRegistry screens;
    uconsole->buildScreens(screens);
    assert(screens.size() == 7);
    return 0;
}

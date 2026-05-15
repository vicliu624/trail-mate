#include "ui_lvgl_ux_packs/ux/ux_screen_menu_adapter.h"
#include "ui_presentation/menu/menu_model.h"

#include <cassert>
#include <cstring>

int main()
{
    ui_lvgl_ux::ScreenRegistry screens;
    assert(screens.add({ui_lvgl_ux::ScreenId::Dashboard, "Dashboard", true}));
    assert(screens.add({ui_lvgl_ux::ScreenId::Chat, "Chat", true}));
    assert(screens.add({ui_lvgl_ux::ScreenId::Map, "Map", false}));
    assert(screens.add({ui_lvgl_ux::ScreenId::Settings, "Settings", true}));

    ui::menu::MenuModel menu;
    ui_lvgl_ux::UxScreenMenuAdapter adapter;
    adapter.buildMenu(screens, menu);

    assert(menu.size() == 3);
    assert(menu.items()[0].screen_id == ui::menu::MenuScreenId::Dashboard);
    assert(menu.items()[1].screen_id == ui::menu::MenuScreenId::Chat);
    assert(menu.items()[2].screen_id == ui::menu::MenuScreenId::Settings);
    assert(std::strcmp(menu.items()[1].label, "Chat") == 0);
    assert(menu.items()[1].enabled);
    return 0;
}

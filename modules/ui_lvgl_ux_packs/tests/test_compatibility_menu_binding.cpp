#include "ui_lvgl_ux_packs/packs/compatibility_ux_pack.h"
#include "ui_lvgl_ux_packs/ux/ux_screen_menu_adapter.h"

#include <cassert>

namespace
{

bool contains(const ui_lvgl_ux::UxMenuModel& menu, ui_lvgl_ux::ScreenId id)
{
    for (std::size_t index = 0; index < menu.size(); ++index)
    {
        if (menu.items()[index].screen_id == id)
        {
            return true;
        }
    }
    return false;
}

} // namespace

int main()
{
    ui_lvgl_ux::CompatibilityUxPack pack;
    ui_lvgl_ux::ScreenRegistry screens;
    ui_lvgl_ux::UxMenuModel menu;
    ui_lvgl_ux::UxScreenMenuAdapter adapter;

    pack.buildScreens(screens);
    adapter.buildMenu(screens, menu);

    assert(menu.size() == 11);
    assert(contains(menu, ui_lvgl_ux::ScreenId::Chat));
    assert(contains(menu, ui_lvgl_ux::ScreenId::Map));
    assert(contains(menu, ui_lvgl_ux::ScreenId::Gps));
    assert(contains(menu, ui_lvgl_ux::ScreenId::Settings));
    return 0;
}

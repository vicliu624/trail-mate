#include "ui_lvgl_ux_packs/packs/compatibility_ux_pack.h"
#include "ui_lvgl_ux_packs/ux/ux_screen_menu_adapter.h"
#include "ui_presentation/menu/menu_model.h"

#include <cassert>

namespace
{

bool contains(const ui::menu::MenuModel& menu, ui::menu::MenuScreenId id)
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
    ui::menu::MenuModel menu;
    ui_lvgl_ux::UxScreenMenuAdapter adapter;

    pack.buildScreens(screens);
    adapter.buildMenu(screens, menu);

    assert(menu.size() == 11);
    assert(contains(menu, ui::menu::MenuScreenId::Chat));
    assert(contains(menu, ui::menu::MenuScreenId::Map));
    assert(contains(menu, ui::menu::MenuScreenId::Gps));
    assert(contains(menu, ui::menu::MenuScreenId::Settings));
    return 0;
}

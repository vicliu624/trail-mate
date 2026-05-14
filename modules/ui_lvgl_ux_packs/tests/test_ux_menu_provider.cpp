#include "ui_lvgl_ux_packs/ux/ux_menu_provider.h"

#include <cassert>
#include <cstring>

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
    ui_lvgl_ux::UxMenuModel menu;

    assert(ui_lvgl_ux::buildMenuForUxPack("compatibility", menu));
    assert(menu.size() >= 8);
    assert(contains(menu, ui_lvgl_ux::ScreenId::Chat));
    assert(contains(menu, ui_lvgl_ux::ScreenId::Map));
    assert(contains(menu, ui_lvgl_ux::ScreenId::Gps));
    assert(contains(menu, ui_lvgl_ux::ScreenId::Settings));

    assert(ui_lvgl_ux::buildMenuForUxPack("uconsole_desktop", menu));
    assert(menu.size() == 7);
    assert(contains(menu, ui_lvgl_ux::ScreenId::Dashboard));
    assert(contains(menu, ui_lvgl_ux::ScreenId::Chat));
    assert(contains(menu, ui_lvgl_ux::ScreenId::Settings));

    assert(!ui_lvgl_ux::buildMenuForUxPack("missing", menu));
    assert(menu.size() == 0);
    assert(!ui_lvgl_ux::buildMenuForUxPack(nullptr, menu));
    assert(menu.size() == 0);
    return 0;
}

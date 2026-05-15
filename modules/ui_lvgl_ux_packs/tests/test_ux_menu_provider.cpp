#include "ui_lvgl_ux_packs/ux/ux_menu_provider.h"
#include "ui_presentation/menu/menu_model.h"

#include <cassert>
#include <cstring>

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
    ui::menu::MenuModel menu;

    assert(ui_lvgl_ux::buildMenuForUxPack("compatibility", menu));
    assert(menu.size() >= 8);
    assert(contains(menu, ui::menu::MenuScreenId::Chat));
    assert(contains(menu, ui::menu::MenuScreenId::Map));
    assert(contains(menu, ui::menu::MenuScreenId::Gps));
    assert(contains(menu, ui::menu::MenuScreenId::Settings));

    assert(ui_lvgl_ux::buildMenuForUxPack("uconsole_desktop", menu));
    assert(menu.size() == 7);
    assert(contains(menu, ui::menu::MenuScreenId::Dashboard));
    assert(contains(menu, ui::menu::MenuScreenId::Chat));
    assert(contains(menu, ui::menu::MenuScreenId::Settings));

    assert(!ui_lvgl_ux::buildMenuForUxPack("missing", menu));
    assert(menu.size() == 0);
    assert(!ui_lvgl_ux::buildMenuForUxPack(nullptr, menu));
    assert(menu.size() == 0);
    return 0;
}

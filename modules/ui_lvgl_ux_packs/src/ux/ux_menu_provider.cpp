#include "ui_lvgl_ux_packs/ux/ux_menu_provider.h"

#include "ui_lvgl_ux_packs/ux/screen_registry.h"
#include "ui_lvgl_ux_packs/ux/ux_pack_registry.h"
#include "ui_lvgl_ux_packs/ux/ux_screen_menu_adapter.h"

namespace ui_lvgl_ux
{

bool buildMenuForUxPack(const char* ux_pack_id, ui::menu::MenuModel& out)
{
    out.clear();

    const IUxPack* pack = findUxPackById(ux_pack_id);
    if (pack == nullptr)
    {
        return false;
    }

    ScreenRegistry screens;
    pack->buildScreens(screens);

    UxScreenMenuAdapter adapter;
    adapter.buildMenu(screens, out);
    return true;
}

} // namespace ui_lvgl_ux

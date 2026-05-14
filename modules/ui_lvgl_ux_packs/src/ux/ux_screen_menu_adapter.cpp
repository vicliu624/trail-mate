#include "ui_lvgl_ux_packs/ux/ux_screen_menu_adapter.h"

namespace ui_lvgl_ux
{

void UxScreenMenuAdapter::buildMenu(const ScreenRegistry& screens, UxMenuModel& out) const
{
    out.clear();

    const ScreenDescriptor* items = screens.items();
    for (std::size_t index = 0; index < screens.size(); ++index)
    {
        const ScreenDescriptor& screen = items[index];
        if (!screen.enabled)
        {
            continue;
        }

        if (!out.add({screen.id, screen.label, true}))
        {
            return;
        }
    }
}

} // namespace ui_lvgl_ux

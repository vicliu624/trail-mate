#include "ui_lvgl_ux_packs/ux/ux_screen_menu_adapter.h"

namespace ui_lvgl_ux
{
namespace
{

ui::menu::MenuScreenId toMenuScreenId(ScreenId id)
{
    switch (id)
    {
    case ScreenId::Dashboard:
        return ui::menu::MenuScreenId::Dashboard;
    case ScreenId::Chat:
        return ui::menu::MenuScreenId::Chat;
    case ScreenId::Contacts:
        return ui::menu::MenuScreenId::Contacts;
    case ScreenId::Map:
        return ui::menu::MenuScreenId::Map;
    case ScreenId::Gps:
        return ui::menu::MenuScreenId::Gps;
    case ScreenId::Team:
        return ui::menu::MenuScreenId::Team;
    case ScreenId::Tracker:
        return ui::menu::MenuScreenId::Tracker;
    case ScreenId::Settings:
        return ui::menu::MenuScreenId::Settings;
    case ScreenId::WalkieTalkie:
        return ui::menu::MenuScreenId::WalkieTalkie;
    case ScreenId::Sstv:
        return ui::menu::MenuScreenId::Sstv;
    case ScreenId::Extensions:
        return ui::menu::MenuScreenId::Extensions;
    }

    return ui::menu::MenuScreenId::Dashboard;
}

} // namespace

void UxScreenMenuAdapter::buildMenu(const ScreenRegistry& screens,
                                    ui::menu::MenuModel& out) const
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

        if (!out.add({toMenuScreenId(screen.id), screen.label, true}))
        {
            return;
        }
    }
}

} // namespace ui_lvgl_ux

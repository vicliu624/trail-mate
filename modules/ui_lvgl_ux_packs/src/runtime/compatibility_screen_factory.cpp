#include "ui_lvgl_ux_packs/runtime/compatibility_screen_factory.h"

namespace ui_lvgl_ux
{
namespace
{

const char* bindingIdForScreen(ui::menu::MenuScreenId screen_id)
{
    switch (screen_id)
    {
    case ui::menu::MenuScreenId::Dashboard:
        return "dashboard";
    case ui::menu::MenuScreenId::Chat:
        return "chat";
    case ui::menu::MenuScreenId::Contacts:
        return "contacts";
    case ui::menu::MenuScreenId::Map:
        return "map";
    case ui::menu::MenuScreenId::Gps:
        return "gps";
    case ui::menu::MenuScreenId::Team:
        return "team";
    case ui::menu::MenuScreenId::Tracker:
        return "tracker";
    case ui::menu::MenuScreenId::Settings:
        return "settings";
    case ui::menu::MenuScreenId::WalkieTalkie:
        return "walkie";
    case ui::menu::MenuScreenId::Sstv:
        return "sstv";
    case ui::menu::MenuScreenId::Extensions:
        return "extensions";
    }

    return nullptr;
}

} // namespace

bool CompatibilityScreenFactory::describe(
    ui::menu::MenuScreenId screen_id,
    CompatibilityScreenDescriptor& out) const
{
    out.screen_id = screen_id;
    out.binding_id = bindingIdForScreen(screen_id);
    out.available = out.binding_id != nullptr;
    return out.available;
}

void CompatibilityScreenFactory::buildBindingsForMenu(
    const ui::menu::MenuModel& menu,
    ui::screen::ScreenBindingRegistry& out) const
{
    out.clear();

    for (std::size_t index = 0; index < menu.size(); ++index)
    {
        const ui::menu::MenuItem& item = menu.items()[index];
        if (!item.enabled)
        {
            continue;
        }

        CompatibilityScreenDescriptor descriptor;
        if (!describe(item.screen_id, descriptor))
        {
            continue;
        }

        if (!out.add({descriptor.screen_id,
                      descriptor.binding_id,
                      descriptor.available}))
        {
            return;
        }
    }
}

} // namespace ui_lvgl_ux

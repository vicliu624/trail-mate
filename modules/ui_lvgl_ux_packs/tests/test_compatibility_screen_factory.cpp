#include "ui_lvgl_ux_packs/runtime/compatibility_screen_factory.h"
#include "ui_lvgl_ux_packs/ux/ux_menu_provider.h"

#include <cassert>
#include <cstring>

namespace
{

void expect_binding(ui_lvgl_ux::CompatibilityScreenFactory& factory,
                    ui::menu::MenuScreenId screen_id,
                    const char* expected_binding)
{
    ui_lvgl_ux::CompatibilityScreenDescriptor descriptor;
    assert(factory.describe(screen_id, descriptor));
    assert(descriptor.screen_id == screen_id);
    assert(descriptor.available);
    assert(std::strcmp(descriptor.binding_id, expected_binding) == 0);
}

} // namespace

int main()
{
    ui_lvgl_ux::CompatibilityScreenFactory factory;

    expect_binding(factory, ui::menu::MenuScreenId::Dashboard, "dashboard");
    expect_binding(factory, ui::menu::MenuScreenId::Chat, "chat");
    expect_binding(factory, ui::menu::MenuScreenId::Contacts, "contacts");
    expect_binding(factory, ui::menu::MenuScreenId::Map, "map");
    expect_binding(factory, ui::menu::MenuScreenId::Gps, "gps");
    expect_binding(factory, ui::menu::MenuScreenId::Team, "team");
    expect_binding(factory, ui::menu::MenuScreenId::Tracker, "tracker");
    expect_binding(factory, ui::menu::MenuScreenId::Settings, "settings");
    expect_binding(factory, ui::menu::MenuScreenId::WalkieTalkie, "walkie");
    expect_binding(factory, ui::menu::MenuScreenId::Sstv, "sstv");
    expect_binding(factory, ui::menu::MenuScreenId::Extensions, "extensions");

    ui::menu::MenuModel menu;
    assert(ui_lvgl_ux::buildMenuForUxPack("compatibility", menu));

    ui::screen::ScreenBindingRegistry bindings;
    factory.buildBindingsForMenu(menu, bindings);
    assert(bindings.size() == menu.size());
    assert(bindings.find(ui::menu::MenuScreenId::Chat) != nullptr);
    assert(bindings.find(ui::menu::MenuScreenId::Map) != nullptr);
    assert(bindings.find(ui::menu::MenuScreenId::Settings) != nullptr);
    return 0;
}

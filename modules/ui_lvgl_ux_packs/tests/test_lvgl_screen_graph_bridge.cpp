#include "product_composition/presentation_bundle.h"
#include "ui_lvgl_ux_packs/runtime/compatibility_screen_factory.h"
#include "ui_lvgl_ux_packs/runtime/lvgl_screen_graph_bridge.h"
#include "ui_lvgl_ux_packs/ux/ux_menu_provider.h"

#include <cassert>
#include <cstddef>
#include <cstring>

namespace
{

bool containsScreen(const ui_lvgl_ux::LvglScreenGraphBridge& bridge,
                    ui::menu::MenuScreenId screen_id,
                    const char* binding_id)
{
    for (std::size_t index = 0; index < bridge.screenCount(); ++index)
    {
        const ui_lvgl_ux::LvglScreenHostEntry& entry =
            bridge.screenItems()[index];
        if (entry.screen_id == screen_id && entry.available &&
            entry.binding_id != nullptr &&
            std::strcmp(entry.binding_id, binding_id) == 0)
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

    ui::screen::ScreenBindingRegistry bindings;
    ui_lvgl_ux::CompatibilityScreenFactory factory;
    factory.buildBindingsForMenu(menu, bindings);

    product_composition::PresentationBundle presentation;
    presentation.ux_menu = &menu;
    presentation.screen_bindings = &bindings;
    assert(product_composition::hasUxMenu(presentation));
    assert(product_composition::hasScreenBindings(presentation));

    ui_lvgl_ux::LvglScreenGraphBridge bridge;
    assert(bridge.load(presentation));
    assert(bridge.menuCount() > 0);
    assert(bridge.screenCount() > 0);
    assert(containsScreen(bridge, ui::menu::MenuScreenId::Chat, "chat"));
    assert(containsScreen(bridge, ui::menu::MenuScreenId::Map, "map"));
    assert(containsScreen(bridge, ui::menu::MenuScreenId::Settings, "settings"));

    product_composition::PresentationBundle empty;
    assert(!bridge.load(empty));
    assert(bridge.menuCount() == 0);
    assert(bridge.screenCount() == 0);
    return 0;
}

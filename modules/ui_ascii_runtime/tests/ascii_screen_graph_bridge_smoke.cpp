#include "linux_sim_composition_root.h"

#include "product_composition/presentation_bundle.h"
#include "ui_ascii_runtime/ascii_screen_graph_bridge.h"

#include <cassert>
#include <cstddef>
#include <cstring>

namespace
{

bool containsScreen(const trailmate::linux_sim::AsciiScreenGraphBridge& bridge,
                    ui::menu::MenuScreenId screen_id,
                    const char* binding_id)
{
    for (std::size_t index = 0; index < bridge.screenCount(); ++index)
    {
        const trailmate::linux_sim::AsciiScreenDescriptor& screen =
            bridge.screenItems()[index];
        if (screen.screen_id == screen_id && screen.available &&
            screen.binding_id != nullptr &&
            std::strcmp(screen.binding_id, binding_id) == 0)
        {
            return true;
        }
    }
    return false;
}

} // namespace

int main()
{
    trailmate::linux_sim::LinuxSimCompositionRoot root;
    assert(root.initialize());
    assert(root.presentation().ux_menu != nullptr);
    assert(product_composition::hasUxMenu(root.presentation()));
    assert(product_composition::hasScreenBindings(root.presentation()));

    trailmate::linux_sim::AsciiScreenGraphBridge bridge;
    assert(bridge.load(root.presentation()));
    assert(bridge.menuCount() > 0);
    assert(bridge.screenCount() > 0);
    assert(bridge.graph().menu_count == bridge.menuCount());
    assert(bridge.graph().screen_count == bridge.screenCount());
    const ui::screen::ScreenRoute route = bridge.menuItems()[0].route;
    assert(route.valid);

    assert(containsScreen(bridge, ui::menu::MenuScreenId::Dashboard, "dashboard"));
    assert(containsScreen(bridge, ui::menu::MenuScreenId::Chat, "chat"));
    assert(containsScreen(bridge, ui::menu::MenuScreenId::Map, "map"));
    assert(containsScreen(bridge, ui::menu::MenuScreenId::Gps, "gps"));
    assert(containsScreen(bridge, ui::menu::MenuScreenId::Settings, "settings"));
    return 0;
}

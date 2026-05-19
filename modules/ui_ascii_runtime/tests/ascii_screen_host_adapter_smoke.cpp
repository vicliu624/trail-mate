#include "linux_sim_composition_root.h"
#include "ui_ascii_runtime/ascii_menu_runtime_adapter.h"
#include "ui_ascii_runtime/ascii_screen_host_adapter.h"

#include "product_composition/presentation_bundle.h"

#include <cassert>
#include <cstddef>
#include <cstring>

int main()
{
    trailmate::linux_sim::LinuxSimCompositionRoot root;
    assert(root.initialize());
    assert(root.presentation().ux_menu != nullptr);
    assert(product_composition::hasUxMenu(root.presentation()));
    assert(product_composition::hasScreenBindings(root.presentation()));

    trailmate::linux_sim::AsciiMenuRuntimeAdapter menu_adapter;
    trailmate::linux_sim::AsciiMenuLine lines[ui::menu::MenuModel::kMaxItems]{};
    std::size_t line_count = 0;
    assert(menu_adapter.buildLines(*root.presentation().ux_menu,
                                   lines,
                                   ui::menu::MenuModel::kMaxItems,
                                   line_count));
    assert(line_count > 0);

    const trailmate::linux_sim::AsciiMenuLine* chat_line = nullptr;
    for (std::size_t index = 0; index < line_count; ++index)
    {
        if (lines[index].screen_id == ui::menu::MenuScreenId::Chat)
        {
            chat_line = &lines[index];
            break;
        }
    }

    assert(chat_line != nullptr);
    assert(chat_line->route.valid);
    const ui::screen::ScreenRoute route = chat_line->route;

    trailmate::linux_sim::AsciiScreenHostAdapter screen_host;
    trailmate::linux_sim::AsciiScreenDescriptor screen;
    assert(screen_host.resolve(route, screen));
    assert(screen.available);
    assert(screen.screen_id == ui::menu::MenuScreenId::Chat);
    assert(std::strcmp(screen.binding_id, "chat") == 0);
    assert(root.presentation().screen_bindings->find(screen.screen_id) != nullptr);
    return 0;
}

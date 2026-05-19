#include "linux_sim_composition_root.h"
#include "ui_ascii_runtime/ascii_menu_runtime_adapter.h"

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

    trailmate::linux_sim::AsciiMenuRuntimeAdapter adapter;
    trailmate::linux_sim::AsciiMenuLine lines[ui::menu::MenuModel::kMaxItems]{};
    std::size_t count = 0;

    assert(adapter.buildLines(*root.presentation().ux_menu,
                              lines,
                              ui::menu::MenuModel::kMaxItems,
                              count));
    assert(count > 0);

    bool found_chat = false;
    for (std::size_t index = 0; index < count; ++index)
    {
        assert(lines[index].enabled);
        assert(lines[index].route.valid);
        assert(lines[index].route.screen_id == lines[index].screen_id);
        assert(lines[index].label != nullptr);
        if (std::strcmp(lines[index].label, "Chat") == 0)
        {
            found_chat = true;
        }
    }

    assert(found_chat);
    return 0;
}

#include "ui_lvgl_ux_packs/ux/screen_registry.h"

#include <cassert>

int main()
{
    ui_lvgl_ux::ScreenRegistry registry;
    assert(registry.size() == 0);

    for (std::size_t i = 0; i < ui_lvgl_ux::ScreenRegistry::kMaxScreens; ++i)
    {
        assert(registry.add({ui_lvgl_ux::ScreenId::Dashboard, "Screen", true}));
    }
    assert(registry.size() == ui_lvgl_ux::ScreenRegistry::kMaxScreens);
    assert(!registry.add({ui_lvgl_ux::ScreenId::Chat, "Overflow", true}));

    registry.clear();
    assert(registry.size() == 0);
    assert(registry.add({ui_lvgl_ux::ScreenId::Map, "Map", true}));
    assert(registry.items()[0].id == ui_lvgl_ux::ScreenId::Map);
    assert(registry.items()[0].enabled);
    return 0;
}

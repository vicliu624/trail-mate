#include "ui_presentation/screen/screen_binding_registry.h"
#include "ui_presentation/screen/screen_route.h"

#include <cassert>
#include <cstring>

int main()
{
    const ui::screen::ScreenRoute route =
        ui::screen::routeForMenuScreen(ui::menu::MenuScreenId::Chat);
    assert(route.valid);
    assert(route.open_mode == ui::screen::ScreenOpenMode::Replace);
    assert(route.screen_id == ui::menu::MenuScreenId::Chat);

    ui::screen::ScreenBindingRegistry registry;
    assert(registry.size() == 0);
    assert(registry.find(ui::menu::MenuScreenId::Chat) == nullptr);

    assert(registry.add({ui::menu::MenuScreenId::Chat, "chat", true}));
    assert(registry.add({ui::menu::MenuScreenId::Map, "map", true}));
    assert(registry.size() == 2);

    const ui::screen::ScreenBinding* chat =
        registry.find(ui::menu::MenuScreenId::Chat);
    assert(chat != nullptr);
    assert(chat->available);
    assert(std::strcmp(chat->binding_id, "chat") == 0);

    assert(registry.find(ui::menu::MenuScreenId::Settings) == nullptr);

    registry.clear();
    assert(registry.size() == 0);
    return 0;
}

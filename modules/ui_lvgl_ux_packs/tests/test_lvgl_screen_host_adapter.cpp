#include "ui_lvgl_ux_packs/runtime/lvgl_screen_host_adapter.h"

#include <cassert>
#include <cstring>

int main()
{
    ui_lvgl_ux::LvglScreenHostAdapter adapter;
    ui_lvgl_ux::LvglScreenHostEntry entry;

    assert(adapter.resolve(ui::menu::MenuScreenId::Chat, entry));
    assert(entry.available);
    assert(entry.screen_id == ui::menu::MenuScreenId::Chat);
    assert(std::strcmp(entry.binding_id, "chat") == 0);

    assert(adapter.resolve(ui::menu::MenuScreenId::Settings, entry));
    assert(entry.available);
    assert(std::strcmp(entry.binding_id, "settings") == 0);
    return 0;
}

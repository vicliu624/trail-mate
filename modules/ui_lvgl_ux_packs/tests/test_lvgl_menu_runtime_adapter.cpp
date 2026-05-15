#include "ui_lvgl_ux_packs/runtime/lvgl_menu_runtime_adapter.h"
#include "ui_lvgl_ux_packs/ux/ux_menu_provider.h"

#include <cassert>
#include <cstddef>
#include <cstring>

int main()
{
    ui::menu::MenuModel menu;
    assert(ui_lvgl_ux::buildMenuForUxPack("compatibility", menu));

    ui_lvgl_ux::LvglMenuRuntimeAdapter adapter;
    ui_lvgl_ux::LvglMenuEntry entries[ui::menu::MenuModel::kMaxItems]{};
    std::size_t count = 0;

    assert(adapter.buildEntries(menu, entries, ui::menu::MenuModel::kMaxItems, count));
    assert(count > 0);
    assert(entries[0].enabled);
    assert(entries[0].route.valid);
    assert(entries[0].route.screen_id == entries[0].screen_id);
    assert(entries[0].label != nullptr);

    std::size_t small_count = 0;
    ui_lvgl_ux::LvglMenuEntry one_entry[1]{};
    assert(!adapter.buildEntries(menu, one_entry, 1, small_count));
    assert(small_count == 1);

    ui::menu::MenuModel empty;
    std::size_t empty_count = 7;
    assert(adapter.buildEntries(empty, nullptr, 0, empty_count));
    assert(empty_count == 0);
    return 0;
}

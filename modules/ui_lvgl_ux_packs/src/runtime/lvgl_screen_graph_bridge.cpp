#include "ui_lvgl_ux_packs/runtime/lvgl_screen_graph_bridge.h"

namespace ui_lvgl_ux
{
namespace
{

using MenuRuntimeAdapterContract = LvglMenuRuntimeAdapter;
using ScreenHostAdapterContract = LvglScreenHostAdapter;

} // namespace

bool LvglScreenGraphBridge::load(
    const product_composition::PresentationBundle& presentation)
{
    menu_count_ = 0;
    screen_count_ = 0;

    if (!product_composition::hasUxMenu(presentation) ||
        !product_composition::hasScreenBindings(presentation))
    {
        return false;
    }

    if (!menu_adapter_.buildEntries(*presentation.ux_menu,
                                    menu_items_,
                                    ui::menu::MenuModel::kMaxItems,
                                    menu_count_))
    {
        menu_count_ = 0;
        return false;
    }

    for (std::size_t index = 0; index < menu_count_; ++index)
    {
        const LvglMenuEntry& menu_entry = menu_items_[index];
        if (!menu_entry.route.valid)
        {
            continue;
        }

        LvglScreenHostEntry screen_entry;
        if (!screen_host_.resolve(menu_entry.screen_id, screen_entry))
        {
            continue;
        }

        if (presentation.screen_bindings->find(screen_entry.screen_id) == nullptr)
        {
            continue;
        }

        screen_items_[screen_count_] = screen_entry;
        ++screen_count_;
    }

    return menu_count_ > 0 && screen_count_ > 0;
}

std::size_t LvglScreenGraphBridge::menuCount() const
{
    return menu_count_;
}

std::size_t LvglScreenGraphBridge::screenCount() const
{
    return screen_count_;
}

const LvglMenuEntry* LvglScreenGraphBridge::menuItems() const
{
    return menu_items_;
}

const LvglScreenHostEntry* LvglScreenGraphBridge::screenItems() const
{
    return screen_items_;
}

} // namespace ui_lvgl_ux

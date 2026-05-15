#include "ui_gtk_runtime/gtk_uconsole_screen_graph_bridge.h"

namespace trailmate::uconsole::gtk
{
namespace
{

using MenuRuntimeAdapterContract = GtkMenuRuntimeAdapter;
using ScreenHostAdapterContract = GtkScreenHostAdapter;

} // namespace

bool GtkUConsoleScreenGraphBridge::load(
    const product_composition::PresentationBundle& presentation)
{
    menu_count_ = 0;
    screen_binding_count_ = 0;

    if (!product_composition::hasUxMenu(presentation) ||
        !product_composition::hasScreenBindings(presentation))
    {
        return false;
    }

    if (!menu_adapter_.buildDescriptors(*presentation.ux_menu,
                                        menu_items_,
                                        ui::menu::MenuModel::kMaxItems,
                                        menu_count_))
    {
        menu_count_ = 0;
        return false;
    }

    for (std::size_t index = 0; index < menu_count_; ++index)
    {
        const trailmate::uconsole::GtkMenuDescriptor& menu_item =
            menu_items_[index];
        if (!menu_item.route.valid)
        {
            continue;
        }

        trailmate::uconsole::GtkScreenDescriptor screen_item;
        if (!screen_host_.resolve(menu_item.route, screen_item))
        {
            continue;
        }

        if (presentation.screen_bindings->find(screen_item.screen_id) == nullptr)
        {
            continue;
        }

        screen_items_[screen_binding_count_] = screen_item;
        ++screen_binding_count_;
    }

    return menu_count_ > 0 && screen_binding_count_ > 0;
}

std::size_t GtkUConsoleScreenGraphBridge::menuCount() const
{
    return menu_count_;
}

std::size_t GtkUConsoleScreenGraphBridge::screenBindingCount() const
{
    return screen_binding_count_;
}

const trailmate::uconsole::GtkMenuDescriptor*
GtkUConsoleScreenGraphBridge::menuItems() const
{
    return menu_items_;
}

const trailmate::uconsole::GtkScreenDescriptor*
GtkUConsoleScreenGraphBridge::screenItems() const
{
    return screen_items_;
}

} // namespace trailmate::uconsole::gtk

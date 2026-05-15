#include "ui_gtk_runtime/gtk_uconsole_screen_graph_presenter.h"

namespace trailmate::uconsole::gtk
{

bool GtkUConsoleScreenGraphPresenter::load(
    const product_composition::PresentationBundle& presentation)
{
    if (!product_composition::hasUxMenu(presentation) ||
        !product_composition::hasScreenBindings(presentation))
    {
        return false;
    }

    return bridge_.load(presentation);
}

std::size_t GtkUConsoleScreenGraphPresenter::menuCount() const
{
    return bridge_.menuCount();
}

std::size_t GtkUConsoleScreenGraphPresenter::screenCount() const
{
    return bridge_.screenBindingCount();
}

const trailmate::uconsole::GtkMenuDescriptor*
GtkUConsoleScreenGraphPresenter::menuDescriptors() const
{
    return bridge_.menuItems();
}

const trailmate::uconsole::GtkScreenDescriptor*
GtkUConsoleScreenGraphPresenter::screenDescriptors() const
{
    return bridge_.screenItems();
}

} // namespace trailmate::uconsole::gtk

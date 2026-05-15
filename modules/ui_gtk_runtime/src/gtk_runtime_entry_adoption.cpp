#include "ui_gtk_runtime/gtk_runtime_entry_adoption.h"

namespace trailmate::uconsole::gtk
{

bool GtkRuntimeEntryAdoption::load(
    const product_composition::PresentationBundle& presentation)
{
    ready_ = presenter_.load(presentation);
    return ready_;
}

bool GtkRuntimeEntryAdoption::ready() const
{
    return ready_;
}

std::size_t GtkRuntimeEntryAdoption::menuCount() const
{
    return ready_ ? presenter_.menuCount() : 0;
}

std::size_t GtkRuntimeEntryAdoption::screenCount() const
{
    return ready_ ? presenter_.screenCount() : 0;
}

const GtkUConsoleScreenGraphPresenter&
GtkRuntimeEntryAdoption::presenter() const
{
    return presenter_;
}

} // namespace trailmate::uconsole::gtk

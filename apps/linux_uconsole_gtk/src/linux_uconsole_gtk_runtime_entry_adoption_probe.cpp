#include "linux_uconsole_gtk_runtime_entry_adoption_probe.h"

#include "ui_lvgl_ux_packs/runtime/compatibility_screen_factory.h"
#include "ui_lvgl_ux_packs/ux/ux_menu_provider.h"

namespace trailmate::apps::linux_uconsole_gtk
{

bool LinuxUConsoleGtkRuntimeEntryAdoptionProbe::load(
    const LinuxUConsoleGtkAppShell& shell)
{
    ready_ = false;
    fallback_ = true;
    menu_.clear();
    screen_bindings_.clear();
    presentation_ = {};

    if (!shell.validate() ||
        !ui_lvgl_ux::buildMenuForUxPack(shell.activeUxPackId(), menu_))
    {
        return false;
    }

    ui_lvgl_ux::CompatibilityScreenFactory screen_factory;
    screen_factory.buildBindingsForMenu(menu_, screen_bindings_);
    presentation_.ux_menu = &menu_;
    presentation_.screen_bindings = &screen_bindings_;

    ready_ = adoption_.load(presentation_);
    fallback_ = !ready_;
    return ready_;
}

bool LinuxUConsoleGtkRuntimeEntryAdoptionProbe::ready() const
{
    return ready_;
}

bool LinuxUConsoleGtkRuntimeEntryAdoptionProbe::fallbackUsed() const
{
    return fallback_;
}

std::size_t LinuxUConsoleGtkRuntimeEntryAdoptionProbe::menuCount() const
{
    return ready_ ? adoption_.menuCount() : 0;
}

std::size_t LinuxUConsoleGtkRuntimeEntryAdoptionProbe::screenCount() const
{
    return ready_ ? adoption_.screenCount() : 0;
}

const trailmate::uconsole::gtk::GtkRuntimeEntryAdoption&
LinuxUConsoleGtkRuntimeEntryAdoptionProbe::adoption() const
{
    return adoption_;
}

} // namespace trailmate::apps::linux_uconsole_gtk

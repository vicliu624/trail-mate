#include "linux_uconsole_gtk_page_registry_adoption.h"

namespace trailmate::apps::linux_uconsole_gtk
{

bool LinuxUConsoleGtkPageRegistryAdoption::load(
    const LinuxUConsoleGtkAppShell& shell)
{
    ready_ = adoption_probe_.load(shell);
    // Phase 9 fallback: hardcoded GTK page registry remains until adoption
    // descriptors become the primary page registry source.
    fallback_ = !ready_;
    return ready_;
}

bool LinuxUConsoleGtkPageRegistryAdoption::ready() const
{
    return ready_;
}

bool LinuxUConsoleGtkPageRegistryAdoption::fallbackUsed() const
{
    return fallback_;
}

std::size_t LinuxUConsoleGtkPageRegistryAdoption::menuCount() const
{
    return ready_ ? adoption_probe_.menuCount() : 0;
}

std::size_t LinuxUConsoleGtkPageRegistryAdoption::screenCount() const
{
    return ready_ ? adoption_probe_.screenCount() : 0;
}

const trailmate::uconsole::GtkMenuDescriptor*
LinuxUConsoleGtkPageRegistryAdoption::menuDescriptors() const
{
    return adoption_probe_.adoption().presenter().menuDescriptors();
}

const trailmate::uconsole::GtkScreenDescriptor*
LinuxUConsoleGtkPageRegistryAdoption::screenDescriptors() const
{
    return adoption_probe_.adoption().presenter().screenDescriptors();
}

const trailmate::uconsole::gtk::GtkRuntimeEntryAdoption&
LinuxUConsoleGtkPageRegistryAdoption::adoption() const
{
    return adoption_probe_.adoption();
}

} // namespace trailmate::apps::linux_uconsole_gtk

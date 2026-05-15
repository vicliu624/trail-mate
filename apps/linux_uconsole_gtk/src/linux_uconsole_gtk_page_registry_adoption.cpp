#include "linux_uconsole_gtk_page_registry_adoption.h"

namespace trailmate::apps::linux_uconsole_gtk
{

bool LinuxUConsoleGtkPageRegistryAdoption::load(
    const LinuxUConsoleGtkAppShell& shell)
{
    ready_ = adoption_probe_.load(shell);
    if (ready_)
    {
        registry_source_ =
            LinuxUConsoleGtkPageRegistrySource::ScreenGraphAdoption;
        fallback_ = false;
        return true;
    }

    registry_source_ = LinuxUConsoleGtkPageRegistrySource::HardcodedFallback;
    fallback_ = true;
    return loadFallback(shell);
}

bool LinuxUConsoleGtkPageRegistryAdoption::loadFallback(
    const LinuxUConsoleGtkAppShell&)
{
    // Phase 10 fallback containment: the legacy hardcoded GTK page registry
    // remains available until descriptors become the primary page source.
    return false;
}

bool LinuxUConsoleGtkPageRegistryAdoption::ready() const
{
    return ready_;
}

bool LinuxUConsoleGtkPageRegistryAdoption::usingPrimaryScreenGraph()
    const noexcept
{
    return registry_source_ ==
               LinuxUConsoleGtkPageRegistrySource::ScreenGraphAdoption &&
           ready_ && !fallback_;
}

bool LinuxUConsoleGtkPageRegistryAdoption::fallbackUsed() const
{
    return fallback_;
}

LinuxUConsoleGtkPageRegistrySource
LinuxUConsoleGtkPageRegistryAdoption::registrySource() const noexcept
{
    return registry_source_;
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

#pragma once

#include "linux_uconsole_gtk_app_shell.h"
#include "linux_uconsole_gtk_runtime_entry_adoption_probe.h"
#include "ui_gtk_runtime/gtk_runtime_entry_adoption.h"

#include <cstddef>

namespace trailmate::apps::linux_uconsole_gtk
{

class LinuxUConsoleGtkPageRegistryAdoption
{
  public:
    bool load(const LinuxUConsoleGtkAppShell& shell);

    bool ready() const;
    bool fallbackUsed() const;
    std::size_t menuCount() const;
    std::size_t screenCount() const;

    const trailmate::uconsole::GtkMenuDescriptor* menuDescriptors() const;
    const trailmate::uconsole::GtkScreenDescriptor* screenDescriptors() const;
    const trailmate::uconsole::gtk::GtkRuntimeEntryAdoption& adoption() const;

  private:
    LinuxUConsoleGtkRuntimeEntryAdoptionProbe adoption_probe_{};
    bool ready_ = false;
    bool fallback_ = true;
};

} // namespace trailmate::apps::linux_uconsole_gtk

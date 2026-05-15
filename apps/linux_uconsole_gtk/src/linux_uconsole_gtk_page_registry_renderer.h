#pragma once

#include "linux_uconsole_gtk_page_registry_adoption.h"
#include "ui_gtk_runtime/gtk_descriptor_page_registry.h"

#include <cstddef>

namespace trailmate::apps::linux_uconsole_gtk
{

class LinuxUConsoleGtkPageRegistryRenderer
{
  public:
    bool render(const LinuxUConsoleGtkPageRegistryAdoption& adoption);

    bool ready() const noexcept;
    bool usingPrimaryScreenGraph() const noexcept;
    bool usedPrimaryScreenGraph() const noexcept;
    bool fallbackUsed() const noexcept;
    bool usedFallback() const noexcept;
    std::size_t pageCount() const noexcept;
    const trailmate::uconsole::GtkDescriptorPage* pages() const noexcept;

  private:
    bool renderFallback(const LinuxUConsoleGtkPageRegistryAdoption& adoption);

  private:
    trailmate::uconsole::GtkDescriptorPageRegistry registry_{};
    bool ready_ = false;
    bool primary_ = false;
    bool fallback_ = true;
};

} // namespace trailmate::apps::linux_uconsole_gtk

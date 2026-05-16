#pragma once

#include "linux_uconsole_gtk_historical_source_descriptor.h"

#include "product_composition/target_profile.h"

namespace trailmate
{
namespace apps
{
namespace linux_uconsole_gtk
{

struct LinuxUConsoleGtkAppShellConfig
{
    const char* target_id = "uconsole";
    const char* ux_pack_id = "uconsole_desktop";
    const char* historical_source =
        linuxUConsoleGtkHistoricalSourceDescriptor().historical_root_name;
};

class LinuxUConsoleGtkAppShell
{
  public:
    LinuxUConsoleGtkAppShell() = default;
    explicit LinuxUConsoleGtkAppShell(LinuxUConsoleGtkAppShellConfig config);

    const LinuxUConsoleGtkAppShellConfig& config() const;
    const char* targetId() const;
    const product_composition::TargetProfile* targetProfile() const;
    const char* activeUxPackId() const;
    bool validate() const;

  private:
    LinuxUConsoleGtkAppShellConfig config_{};
};

} // namespace linux_uconsole_gtk
} // namespace apps
} // namespace trailmate

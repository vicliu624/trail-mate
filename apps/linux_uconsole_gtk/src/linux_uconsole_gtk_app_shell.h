#pragma once

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
    const char* transitional_source = "apps/linux_uconsole";
    const char* legacy_adapter_target = "trailmate_linux_uconsole_legacy_adapter";
};

class LinuxUConsoleGtkAppShell
{
  public:
    const LinuxUConsoleGtkAppShellConfig& config() const;
    const char* activeUxPackId() const;
    bool validate() const;

  private:
    LinuxUConsoleGtkAppShellConfig config_{};
};

} // namespace linux_uconsole_gtk
} // namespace apps
} // namespace trailmate

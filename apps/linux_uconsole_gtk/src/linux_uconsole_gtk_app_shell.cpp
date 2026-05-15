#include "linux_uconsole_gtk_app_shell.h"

#include "ui_lvgl_ux_packs/ux/ux_pack_registry.h"

namespace trailmate
{
namespace apps
{
namespace linux_uconsole_gtk
{

LinuxUConsoleGtkAppShell::LinuxUConsoleGtkAppShell(
    LinuxUConsoleGtkAppShellConfig config)
    : config_(config)
{
}

const LinuxUConsoleGtkAppShellConfig& LinuxUConsoleGtkAppShell::config() const
{
    return config_;
}

const char* LinuxUConsoleGtkAppShell::activeUxPackId() const
{
    return config_.ux_pack_id;
}

bool LinuxUConsoleGtkAppShell::validate() const
{
    const auto& legacy_source = linuxUConsoleGtkLegacySourceDescriptor();
    return config_.target_id != nullptr &&
           config_.ux_pack_id != nullptr &&
           ui_lvgl_ux::findUxPackById(activeUxPackId()) != nullptr &&
           config_.transitional_source != nullptr &&
           legacy_source.root_path != nullptr &&
           legacy_source.historical_name != nullptr &&
           legacy_source.replacement_owner != nullptr;
}

} // namespace linux_uconsole_gtk
} // namespace apps
} // namespace trailmate

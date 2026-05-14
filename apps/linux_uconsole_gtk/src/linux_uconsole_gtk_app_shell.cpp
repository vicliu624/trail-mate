#include "linux_uconsole_gtk_app_shell.h"

namespace trailmate
{
namespace apps
{
namespace linux_uconsole_gtk
{

const LinuxUConsoleGtkAppShellConfig& LinuxUConsoleGtkAppShell::config() const
{
    return config_;
}

bool LinuxUConsoleGtkAppShell::validate() const
{
    return config_.target_id != nullptr &&
           config_.ux_pack_id != nullptr &&
           config_.transitional_source != nullptr &&
           config_.legacy_adapter_target != nullptr;
}

} // namespace linux_uconsole_gtk
} // namespace apps
} // namespace trailmate

#include "linux_uconsole_gtk_app_shell.h"

#include "product_composition/target_ux_binding.h"
#include "ui_lvgl_ux_packs/ux/ux_pack_registry.h"

#include <cstring>

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

const char* LinuxUConsoleGtkAppShell::targetId() const
{
    return config_.target_id;
}

const product_composition::TargetProfile* LinuxUConsoleGtkAppShell::targetProfile() const
{
    return product_composition::findTargetProfile(targetId());
}

const char* LinuxUConsoleGtkAppShell::activeUxPackId() const
{
    const auto* binding = product_composition::findTargetUxBinding(targetId());
    return binding != nullptr ? binding->active_ux_pack_id : nullptr;
}

bool LinuxUConsoleGtkAppShell::validate() const
{
    const auto& historical_source =
        linuxUConsoleGtkHistoricalSourceDescriptor();
    return config_.target_id != nullptr &&
           targetProfile() != nullptr &&
           product_composition::findTargetUxBinding(targetId()) != nullptr &&
           config_.ux_pack_id != nullptr &&
           std::strcmp(config_.ux_pack_id, activeUxPackId()) == 0 &&
           ui_lvgl_ux::findUxPackById(activeUxPackId()) != nullptr &&
           config_.historical_source != nullptr &&
           historical_source.historical_root_name != nullptr &&
           historical_source.historical_role != nullptr &&
           historical_source.replacement_owner != nullptr;
}

} // namespace linux_uconsole_gtk
} // namespace apps
} // namespace trailmate

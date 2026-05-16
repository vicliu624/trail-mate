#include "linux_sim_app_shell.h"

#include "ui_lvgl_ux_packs/ux/ux_pack_registry.h"

namespace trailmate
{
namespace apps
{
namespace linux_sim_shell
{

LinuxSimAppShell::LinuxSimAppShell(LinuxSimAppShellConfig config)
    : config_(config)
{
}

const LinuxSimAppShellConfig& LinuxSimAppShell::config() const
{
    return config_;
}

const char* LinuxSimAppShell::activeUxPackId() const
{
    return config_.ux_pack_id;
}

bool LinuxSimAppShell::validate() const
{
    const auto& historical_source = linuxSimHistoricalSourceDescriptor();
    return config_.target_id != nullptr &&
           config_.ux_pack_id != nullptr &&
           ui_lvgl_ux::findUxPackById(activeUxPackId()) != nullptr &&
           config_.historical_source != nullptr &&
           historical_source.historical_root_name != nullptr &&
           historical_source.historical_role != nullptr &&
           historical_source.replacement_owner != nullptr;
}

} // namespace linux_sim_shell
} // namespace apps
} // namespace trailmate

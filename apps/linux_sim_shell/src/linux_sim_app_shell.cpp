#include "linux_sim_app_shell.h"

#include "product_composition/target_ux_binding.h"
#include "ui_lvgl_ux_packs/ux/ux_pack_registry.h"

#include <cstring>

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

const char* LinuxSimAppShell::targetId() const
{
    return config_.product_target_id;
}

const product_composition::TargetProfile* LinuxSimAppShell::targetProfile() const
{
    return product_composition::findTargetProfile(targetId());
}

const char* LinuxSimAppShell::activeUxPackId() const
{
    const auto* binding = product_composition::findTargetUxBinding(targetId());
    return binding != nullptr ? binding->active_ux_pack_id : nullptr;
}

bool LinuxSimAppShell::validate() const
{
    const auto& historical_source = linuxSimHistoricalSourceDescriptor();
    return config_.target_id != nullptr &&
           config_.product_target_id != nullptr &&
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

} // namespace linux_sim_shell
} // namespace apps
} // namespace trailmate

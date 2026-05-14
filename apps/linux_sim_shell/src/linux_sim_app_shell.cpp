#include "linux_sim_app_shell.h"

#include "ui_lvgl_ux_packs/ux/ux_pack_registry.h"

namespace trailmate
{
namespace apps
{
namespace linux_sim_shell
{

const LinuxSimAppShellConfig& LinuxSimAppShell::config() const
{
    return config_;
}

bool LinuxSimAppShell::validate() const
{
    return config_.target_id != nullptr &&
           config_.ux_pack_id != nullptr &&
           ui_lvgl_ux::findUxPackById(config_.ux_pack_id) != nullptr &&
           config_.transitional_source != nullptr &&
           config_.legacy_adapter_target != nullptr;
}

} // namespace linux_sim_shell
} // namespace apps
} // namespace trailmate

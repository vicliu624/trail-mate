#include "linux_sim_app_shell.h"

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
           config_.transitional_source != nullptr;
}

} // namespace linux_sim_shell
} // namespace apps
} // namespace trailmate

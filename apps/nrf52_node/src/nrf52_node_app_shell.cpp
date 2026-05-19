#include "nrf52_node_app_shell.h"

#include "product_composition/target_ux_binding.h"

#include <cstring>

namespace trailmate
{
namespace apps
{
namespace nrf52_node
{

const Nrf52NodeAppShellConfig& Nrf52NodeAppShell::config() const
{
    return config_;
}

const char* Nrf52NodeAppShell::targetId() const
{
    return config_.target_id;
}

const product_composition::TargetProfile* Nrf52NodeAppShell::targetProfile() const
{
    return product_composition::findTargetProfile(targetId());
}

const char* Nrf52NodeAppShell::activeUxPackId() const
{
    const auto* binding = product_composition::findTargetUxBinding(targetId());
    return binding != nullptr ? binding->active_ux_pack_id : nullptr;
}

bool Nrf52NodeAppShell::validate() const
{
    return config_.target_id != nullptr &&
           targetProfile() != nullptr &&
           product_composition::findTargetUxBinding(targetId()) != nullptr &&
           config_.target_family != nullptr &&
           config_.default_ux_pack_id != nullptr &&
           std::strcmp(config_.default_ux_pack_id, activeUxPackId()) == 0 &&
           config_.historical_generic_root_name != nullptr &&
           config_.historical_board_root_name != nullptr &&
           config_.historical_role != nullptr &&
           config_.replacement_owner != nullptr;
}

} // namespace nrf52_node
} // namespace apps
} // namespace trailmate

#include "nrf52_node_app_shell.h"

#include "ui_lvgl_ux_packs/ux/ux_pack_registry.h"

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

const char* Nrf52NodeAppShell::activeUxPackId() const
{
    return config_.default_ux_pack_id;
}

bool Nrf52NodeAppShell::validate() const
{
    return config_.target_family != nullptr &&
           config_.default_ux_pack_id != nullptr &&
           ui_lvgl_ux::findUxPackById(activeUxPackId()) != nullptr &&
           config_.historical_generic_root_name != nullptr &&
           config_.historical_board_root_name != nullptr &&
           config_.historical_role != nullptr &&
           config_.replacement_owner != nullptr;
}

} // namespace nrf52_node
} // namespace apps
} // namespace trailmate

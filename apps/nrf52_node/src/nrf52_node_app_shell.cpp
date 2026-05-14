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

bool Nrf52NodeAppShell::validate() const
{
    return config_.target_family != nullptr &&
           config_.default_ux_pack_id != nullptr &&
           ui_lvgl_ux::findUxPackById(config_.default_ux_pack_id) != nullptr &&
           config_.transitional_source != nullptr &&
           config_.board_specific_transitional_source != nullptr &&
           config_.legacy_adapter_target != nullptr;
}

} // namespace nrf52_node
} // namespace apps
} // namespace trailmate

#include "esp32_lvgl_app_shell.h"

#include "product_composition/target_ux_binding.h"
#include "ui_lvgl_ux_packs/ux/ux_pack_registry.h"

#include <cstring>

namespace trailmate
{
namespace apps
{
namespace esp32_lvgl
{

const Esp32LvglAppShellConfig& Esp32LvglAppShell::config() const
{
    return config_;
}

const char* Esp32LvglAppShell::targetId() const
{
    return config_.target_id;
}

const product_composition::TargetProfile* Esp32LvglAppShell::targetProfile() const
{
    return product_composition::findTargetProfile(targetId());
}

const char* Esp32LvglAppShell::activeUxPackId() const
{
    const auto* binding = product_composition::findTargetUxBinding(targetId());
    return binding != nullptr ? binding->active_ux_pack_id : nullptr;
}

bool Esp32LvglAppShell::validate() const
{
    return config_.target_id != nullptr &&
           targetProfile() != nullptr &&
           product_composition::findTargetUxBinding(targetId()) != nullptr &&
           config_.target_family != nullptr &&
           config_.default_ux_pack_id != nullptr &&
           std::strcmp(config_.default_ux_pack_id, activeUxPackId()) == 0 &&
           ui_lvgl_ux::findUxPackById(activeUxPackId()) != nullptr &&
           config_.build_entrypoint != nullptr &&
           config_.component_sources != nullptr &&
           config_.historical_root_name != nullptr &&
           config_.historical_role != nullptr &&
           config_.replacement_owner != nullptr;
}

} // namespace esp32_lvgl
} // namespace apps
} // namespace trailmate

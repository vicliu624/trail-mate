#include "esp32_lvgl_app_shell.h"

#include "ui_lvgl_ux_packs/ux/ux_pack_registry.h"

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

const char* Esp32LvglAppShell::activeUxPackId() const
{
    return config_.default_ux_pack_id;
}

bool Esp32LvglAppShell::validate() const
{
    return config_.target_family != nullptr &&
           config_.default_ux_pack_id != nullptr &&
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

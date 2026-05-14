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

bool Esp32LvglAppShell::validate() const
{
    return config_.target_family != nullptr &&
           config_.default_ux_pack_id != nullptr &&
           ui_lvgl_ux::findUxPackById(config_.default_ux_pack_id) != nullptr &&
           config_.transitional_source != nullptr &&
           config_.legacy_adapter_target != nullptr;
}

} // namespace esp32_lvgl
} // namespace apps
} // namespace trailmate

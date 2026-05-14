#include "linux_sim_app_shell.h"

#include "ui_lvgl_ux_packs/ux/ux_pack_registry.h"

#include <cassert>
#include <cstring>

int main()
{
    trailmate::apps::linux_sim_shell::LinuxSimAppShell shell;
    assert(shell.validate());

    const auto& config = shell.config();
    assert(std::strcmp(config.target_id, "linux_sim") == 0);
    assert(std::strcmp(config.ux_pack_id, "simulator_full") == 0);
    assert(std::strcmp(config.transitional_source, "apps/linux_sim") == 0);
    assert(std::strcmp(config.legacy_adapter_target,
                       "trailmate_linux_sim_legacy_adapter") == 0);
    assert(ui_lvgl_ux::findUxPackById(config.ux_pack_id) != nullptr);
    return 0;
}

#include "linux_sim_app_shell.h"

#include "ui_lvgl_ux_packs/ux/ux_menu_provider.h"
#include "ui_lvgl_ux_packs/ux/ux_pack_registry.h"
#include "ui_presentation/menu/menu_model.h"

#include <cassert>
#include <cstring>

int main()
{
    trailmate::apps::linux_sim_shell::LinuxSimAppShell shell;
    assert(shell.validate());

    const auto& config = shell.config();
    assert(std::strcmp(config.target_id, "linux_sim") == 0);
    assert(std::strcmp(config.ux_pack_id, "simulator_full") == 0);
    assert(std::strcmp(shell.activeUxPackId(), "simulator_full") == 0);
    assert(std::strcmp(config.transitional_source, "legacy/app_implementations/linux_sim") == 0);
    assert(std::strcmp(config.legacy_adapter_target,
                       "trailmate_linux_sim_legacy_adapter") == 0);
    assert(ui_lvgl_ux::findUxPackById(shell.activeUxPackId()) != nullptr);

    ui::menu::MenuModel menu;
    assert(ui_lvgl_ux::buildMenuForUxPack(shell.activeUxPackId(), menu));
    assert(menu.size() > 0);
    return 0;
}

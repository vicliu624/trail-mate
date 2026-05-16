#include "linux_sim_app_shell.h"
#include "linux_sim_historical_source_descriptor.h"

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
    assert(std::strcmp(config.product_target_id, "cardputerzero") == 0);
    assert(std::strcmp(shell.targetId(), "cardputerzero") == 0);
    assert(std::strcmp(config.ux_pack_id, "simulator_full") == 0);
    assert(std::strcmp(shell.activeUxPackId(), "simulator_full") == 0);
    assert(shell.targetProfile() != nullptr);
    assert(std::strcmp(shell.targetProfile()->app_shell, "apps/linux_sim_shell") == 0);
    const auto& descriptor =
        trailmate::apps::linux_sim_shell::linuxSimHistoricalSourceDescriptor();
    assert(std::strcmp(config.historical_source,
                       descriptor.historical_root_name) == 0);
    assert(std::strcmp(descriptor.historical_role,
                       "pre-refactor Linux simulator implementation root") == 0);
    assert(std::strcmp(descriptor.replacement_owner,
                       "apps/linux_sim_shell + modules/ui_ascii_runtime") == 0);
    assert(ui_lvgl_ux::findUxPackById(shell.activeUxPackId()) != nullptr);

    ui::menu::MenuModel menu;
    assert(ui_lvgl_ux::buildMenuForUxPack(shell.activeUxPackId(), menu));
    assert(menu.size() > 0);
    return 0;
}

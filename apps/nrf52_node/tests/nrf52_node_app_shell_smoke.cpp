#include "nrf52_node_app_shell.h"

#include "ui_lvgl_ux_packs/ux/ux_menu_provider.h"
#include "ui_lvgl_ux_packs/ux/ux_pack_registry.h"
#include "ui_presentation/menu/menu_model.h"

#include <cassert>
#include <cstring>

int main()
{
    trailmate::apps::nrf52_node::Nrf52NodeAppShell shell;
    assert(shell.validate());

    const auto& config = shell.config();
    assert(std::strcmp(config.target_family, "nrf52_node") == 0);
    assert(std::strcmp(config.default_ux_pack_id, "tiny_node_status") == 0);
    assert(std::strcmp(shell.activeUxPackId(), "tiny_node_status") == 0);
    assert(std::strcmp(config.transitional_source, "legacy/app_implementations/esp_pio") == 0);
    assert(std::strcmp(config.board_specific_transitional_source,
                       "legacy/app_implementations/gat562_mesh_evb_pro") == 0);
    assert(std::strcmp(config.legacy_adapter_target,
                       "trailmate_nrf52_pio_legacy_adapter") == 0);
    assert(ui_lvgl_ux::findUxPackById(shell.activeUxPackId()) != nullptr);

    ui::menu::MenuModel menu;
    assert(ui_lvgl_ux::buildMenuForUxPack(shell.activeUxPackId(), menu));
    assert(menu.size() > 0);
    return 0;
}

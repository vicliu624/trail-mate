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
    assert(std::strcmp(config.target_id, "gat562_mesh_evb_pro") == 0);
    assert(std::strcmp(shell.targetId(), "gat562_mesh_evb_pro") == 0);
    assert(std::strcmp(config.target_family, "nrf52_node") == 0);
    assert(std::strcmp(config.default_ux_pack_id, "tiny_node_status") == 0);
    assert(std::strcmp(shell.activeUxPackId(), "tiny_node_status") == 0);
    assert(shell.targetProfile() != nullptr);
    assert(shell.targetProfile()->renderer == product_composition::TargetRenderer::Headless);
    assert(std::strcmp(config.historical_generic_root_name,
                       "legacy/app_implementations/esp_pio") == 0);
    assert(std::strcmp(config.historical_board_root_name,
                       "legacy/app_implementations/gat562_mesh_evb_pro") == 0);
    assert(std::strcmp(config.historical_role,
                       "pre-refactor PlatformIO/nRF52 implementation roots") == 0);
    assert(std::strcmp(config.replacement_owner,
                       "apps/nrf52_node + builds/pio_nrf52 + boards/gat562_mesh_evb_pro") == 0);
    assert(ui_lvgl_ux::findUxPackById(shell.activeUxPackId()) != nullptr);

    ui::menu::MenuModel menu;
    assert(ui_lvgl_ux::buildMenuForUxPack(shell.activeUxPackId(), menu));
    assert(menu.size() > 0);
    return 0;
}

#include "esp32_lvgl_app_shell.h"

#include "ui_lvgl_ux_packs/ux/ux_menu_provider.h"
#include "ui_lvgl_ux_packs/ux/ux_pack_registry.h"
#include "ui_presentation/menu/menu_model.h"

#include <cassert>
#include <cstring>

int main()
{
    trailmate::apps::esp32_lvgl::Esp32LvglAppShell shell;
    assert(shell.validate());

    const auto& config = shell.config();
    assert(std::strcmp(config.target_family, "esp32_lvgl") == 0);
    assert(std::strcmp(config.default_ux_pack_id, "compatibility") == 0);
    assert(std::strcmp(shell.activeUxPackId(), "compatibility") == 0);
    assert(std::strcmp(config.historical_root_name,
                       "legacy/app_implementations/esp_idf") == 0);
    assert(std::strcmp(config.historical_role,
                       "pre-refactor ESP-IDF/LVGL implementation root") == 0);
    assert(std::strcmp(config.replacement_owner,
                       "apps/esp32_lvgl + builds/esp_idf") == 0);
    assert(std::strcmp(config.legacy_adapter_target,
                       "trailmate_esp_idf_legacy_adapter") == 0);
    assert(ui_lvgl_ux::findUxPackById(shell.activeUxPackId()) != nullptr);

    ui::menu::MenuModel menu;
    assert(ui_lvgl_ux::buildMenuForUxPack(shell.activeUxPackId(), menu));
    assert(menu.size() > 0);
    return 0;
}

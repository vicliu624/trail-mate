#include "esp32_lvgl_app_shell.h"

#include <cassert>
#include <cstring>

int main()
{
    trailmate::apps::esp32_lvgl::Esp32LvglAppShell shell;
    assert(shell.validate());

    const auto& config = shell.config();
    assert(std::strcmp(config.target_family, "esp32_lvgl") == 0);
    assert(std::strcmp(config.default_ux_pack_id, "compatibility") == 0);
    assert(std::strcmp(config.transitional_source, "apps/esp_idf") == 0);
    assert(std::strcmp(config.legacy_adapter_target,
                       "trailmate_esp_idf_legacy_adapter") == 0);
    return 0;
}

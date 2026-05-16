#include "product_composition/target_profile.h"

#include <cassert>
#include <cstring>

int main()
{
    std::size_t count = 0;
    const auto* profiles = product_composition::esp32LvglTargetProfiles(&count);
    assert(profiles != nullptr);
    assert(count == 3);

    const auto* tab5 = product_composition::findTargetProfile("tab5");
    assert(tab5 != nullptr);
    assert(std::strcmp(tab5->board_id, "tab5") == 0);
    assert(std::strcmp(tab5->build_entrypoint, "builds/esp_idf") == 0);
    assert(std::strcmp(tab5->app_shell, "apps/esp32_lvgl") == 0);
    assert(tab5->platform == product_composition::TargetPlatform::EspIdf);
    assert(tab5->renderer == product_composition::TargetRenderer::Lvgl);
    assert(std::strcmp(tab5->ux_pack_id, "compatibility") == 0);

    const auto* tft = product_composition::findTargetProfile("tdisplayp4_tft");
    assert(tft != nullptr);
    assert(std::strcmp(tft->board_id, "t_display_p4") == 0);

    const auto* amoled = product_composition::findTargetProfile("tdisplayp4_amoled");
    assert(amoled != nullptr);
    assert(std::strcmp(amoled->board_id, "t_display_p4") == 0);

    assert(product_composition::findTargetProfile("twatch") == nullptr);
    return 0;
}

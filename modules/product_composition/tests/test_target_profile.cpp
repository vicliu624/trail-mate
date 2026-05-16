#include "product_composition/target_profile.h"

#include <cassert>
#include <cstring>

int main()
{
    std::size_t count = 0;
    const auto* all = product_composition::allTargetProfiles(&count);
    assert(all != nullptr);
    assert(count == 9);

    const auto* profiles = product_composition::esp32LvglTargetProfiles(&count);
    assert(profiles != nullptr);
    assert(count == 6);

    const auto* tab5 = product_composition::findTargetProfile("tab5");
    assert(tab5 != nullptr);
    assert(std::strcmp(tab5->board_id, "tab5") == 0);
    assert(std::strcmp(tab5->build_entrypoint, "builds/esp_idf") == 0);
    assert(std::strcmp(tab5->app_shell, "apps/esp32_lvgl") == 0);
    assert(std::strcmp(tab5->ux_pack_id, "tab5_touch") == 0);
    assert(std::strcmp(tab5->ui_profile_id, "tab5_touch_ui") == 0);
    assert(std::strcmp(tab5->page_manifest_id, "tab5_touch_manifest") == 0);
    assert(std::strcmp(tab5->layout_profile_id, "tab5_large_touch") == 0);
    assert(tab5->platform == product_composition::TargetPlatform::EspIdf);
    assert(tab5->renderer == product_composition::TargetRenderer::Lvgl);
    assert(tab5->status == product_composition::TargetSupportStatus::ActiveWithFallback);
    assert(tab5->has_display);
    assert(tab5->has_touch);
    assert(tab5->has_lora);
    assert(tab5->has_gps);
    assert(tab5->has_audio);

    const auto* tft = product_composition::findTargetProfile("tdisplayp4_tft");
    assert(tft != nullptr);
    assert(std::strcmp(tft->board_id, "tdisplayp4") == 0);

    const auto* amoled = product_composition::findTargetProfile("tdisplayp4_amoled");
    assert(amoled != nullptr);
    assert(std::strcmp(amoled->board_id, "tdisplayp4") == 0);

    const auto* pager = product_composition::findTargetProfile("tlora_pager");
    assert(pager != nullptr);
    assert(pager->status == product_composition::TargetSupportStatus::PendingHardwareValidation);
    assert(pager->has_keyboard);
    assert(!pager->has_touch);

    const auto* deck = product_composition::findTargetProfile("tdeck");
    assert(deck != nullptr);
    assert(deck->has_keyboard);
    assert(deck->has_trackball);

    const auto* watch = product_composition::findTargetProfile("twatch");
    assert(watch != nullptr);
    assert(watch->has_touch);
    assert(!watch->has_gps);

    const auto* uconsole = product_composition::findTargetProfile("uconsole");
    assert(uconsole != nullptr);
    assert(uconsole->renderer == product_composition::TargetRenderer::Gtk);
    assert(uconsole->platform == product_composition::TargetPlatform::Linux);

    const auto* cardputer = product_composition::findTargetProfile("cardputerzero");
    assert(cardputer != nullptr);
    assert(cardputer->renderer == product_composition::TargetRenderer::Ascii);

    const auto* gat562 = product_composition::findTargetProfile("gat562_mesh_evb_pro");
    assert(gat562 != nullptr);
    assert(gat562->renderer == product_composition::TargetRenderer::Headless);
    assert(gat562->status == product_composition::TargetSupportStatus::Headless);
    assert(gat562->has_display);
    assert(gat562->has_lora);
    assert(gat562->has_gps);

    assert(product_composition::findTargetProfile("unknown") == nullptr);
    return 0;
}

#include "product_composition/target_profile.h"

#include <cassert>
#include <cstring>

int main()
{
    std::size_t count = 0;
    const auto* all = product_composition::allTargetProfiles(&count);
    assert(all != nullptr);
    assert(count == 12);

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
    assert(tab5->has_motion_sensor);
    assert(tab5->has_wireless_companion);
    assert(tab5->ble_backend == product_composition::BleBackend::C6Companion);
    assert(tab5->wireless_companion ==
           product_composition::WirelessCompanionKind::Esp32C6);
    assert(tab5->display_orientation_policy ==
           product_composition::DisplayOrientationPolicy::SensorLandscapeOnly);

    const auto* tft = product_composition::findTargetProfile("t_display_p4_tft");
    assert(tft != nullptr);
    assert(std::strcmp(tft->board_id, "t_display_p4") == 0);
    assert(tft->has_motion_sensor);
    assert(tft->has_wireless_companion);
    assert(tft->ble_backend == product_composition::BleBackend::C6Companion);
    assert(tft->status == product_composition::TargetSupportStatus::Active);
    assert(tft->display_orientation_policy ==
           product_composition::DisplayOrientationPolicy::SensorLandscapeOnly);

    const auto* amoled = product_composition::findTargetProfile("t_display_p4_amoled");
    assert(amoled != nullptr);
    assert(std::strcmp(amoled->board_id, "t_display_p4") == 0);
    assert(amoled->has_motion_sensor);
    assert(amoled->has_wireless_companion);
    assert(amoled->ble_backend == product_composition::BleBackend::C6Companion);
    assert(amoled->status == product_composition::TargetSupportStatus::Active);
    assert(amoled->display_orientation_policy ==
           product_composition::DisplayOrientationPolicy::SensorLandscapeOnly);

    const auto* pager = product_composition::findTargetProfile("tlora_pager");
    assert(pager != nullptr);
    assert(pager->status == product_composition::TargetSupportStatus::PendingHardwareValidation);
    assert(pager->has_keyboard);
    assert(!pager->has_touch);
    assert(!pager->has_wireless_companion);
    assert(pager->ble_backend == product_composition::BleBackend::Local);

    const auto* deck = product_composition::findTargetProfile("tdeck");
    assert(deck != nullptr);
    assert(deck->has_keyboard);
    assert(deck->has_trackball);
    assert(deck->ble_backend == product_composition::BleBackend::Local);

    const auto* watch = product_composition::findTargetProfile("twatch");
    assert(watch != nullptr);
    assert(watch->has_touch);
    assert(!watch->has_gps);

    const auto* uconsole = product_composition::findTargetProfile("uconsole");
    assert(uconsole != nullptr);
    assert(uconsole->renderer == product_composition::TargetRenderer::Gtk);
    assert(uconsole->platform == product_composition::TargetPlatform::Linux);

    const auto* linux_sim = product_composition::findTargetProfile("linux_sim");
    assert(linux_sim != nullptr);
    assert(linux_sim->renderer == product_composition::TargetRenderer::Ascii);
    assert(linux_sim->platform == product_composition::TargetPlatform::Linux);
    assert(std::strcmp(linux_sim->app_shell, "apps/linux_sim_shell") == 0);
    assert(linux_sim->status == product_composition::TargetSupportStatus::Active);

    const auto* cardputer = product_composition::findTargetProfile("cardputerzero");
    assert(cardputer != nullptr);
    assert(cardputer->renderer == product_composition::TargetRenderer::Ascii);
    assert(cardputer->platform == product_composition::TargetPlatform::Linux);
    assert(std::strcmp(cardputer->app_shell, "apps/linux_cardputer_zero") == 0);
    assert(cardputer->status ==
           product_composition::TargetSupportStatus::PendingHardwareValidation);
    assert(cardputer->has_lora);
    assert(cardputer->has_gps);

    const auto* gat562 = product_composition::findTargetProfile("gat562_mesh_evb_pro");
    assert(gat562 != nullptr);
    assert(gat562->renderer == product_composition::TargetRenderer::Headless);
    assert(gat562->status == product_composition::TargetSupportStatus::Headless);
    assert(gat562->has_display);
    assert(gat562->has_lora);
    assert(gat562->has_gps);

    const auto* impulse = product_composition::findTargetProfile("t-impulse-plus");
    assert(impulse != nullptr);
    assert(std::strcmp(impulse->board_id, "lilygo_t_impulse_plus_nrf52840") == 0);
    assert(std::strcmp(impulse->layout_profile_id, "screen_64x32") == 0);
    assert(impulse->renderer == product_composition::TargetRenderer::Headless);
    assert(impulse->has_display);
    assert(impulse->has_touch);
    assert(impulse->has_lora);
    assert(impulse->has_gps);
    assert(!impulse->has_keyboard);
    assert(!impulse->has_motion_sensor);
    assert(impulse->ble_backend == product_composition::BleBackend::Local);

    assert(product_composition::findTargetProfile("unknown") == nullptr);
    return 0;
}

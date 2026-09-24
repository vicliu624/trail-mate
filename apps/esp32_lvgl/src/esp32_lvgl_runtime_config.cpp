#include "esp32_lvgl_runtime_config.h"

#include "product_composition/target_profile.h"
#include "product_composition/target_ux_binding.h"

#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif

namespace trailmate::apps::esp32_lvgl
{

const Esp32LvglRuntimeConfig& esp32LvglRuntimeConfig()
{
#if defined(ARDUINO_WIO_TRACKER_L2)
    static const Esp32LvglRuntimeConfig kConfig = {
        "wio_tracker_l2",
        "trail-mate-wio-tracker-l2",
        "Wio Tracker L2",
        "wio_l2_app_loop",
        10,
        12288,
        5,
    };
#elif defined(ARDUINO_T_LORA_PAGER)
    static const Esp32LvglRuntimeConfig kConfig = {
        "tlora_pager",
        "trail-mate-tlora-pager",
        "T-LoRa Pager",
        "pager_app_loop",
        10,
        12288,
        5,
    };
#elif defined(ARDUINO_T_DECK)
    static const Esp32LvglRuntimeConfig kConfig = {
        "tdeck",
        "trail-mate-tdeck",
        "T-Deck",
        "tdeck_app_loop",
        10,
        12288,
        5,
    };
#elif defined(TRAIL_MATE_ESP_BOARD_TAB5)
    static const Esp32LvglRuntimeConfig kConfig = {
        "tab5",
        "trail-mate-tab5",
        "Tab5",
        "tab5_app_loop",
        10,
        4096,
        5,
    };
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
#if defined(CONFIG_TRAIL_MATE_T_DISPLAY_P4_PANEL_RM69A10) || defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4_AMOLED)
    static const Esp32LvglRuntimeConfig kConfig = {
        "t_display_p4_amoled",
        "trail-mate-t-display-p4-amoled",
        "T-Display-P4 AMOLED",
        "t_display_p4_amoled_app_loop",
        10,
        12288,
        5,
    };
#else
    static const Esp32LvglRuntimeConfig kConfig = {
        "t_display_p4_tft",
        "trail-mate-t-display-p4-tft",
        "T-Display-P4 TFT",
        "t_display_p4_tft_app_loop",
        10,
        12288,
        5,
    };
#endif
#else
    static const Esp32LvglRuntimeConfig kConfig = {
        "esp_idf",
        "trail-mate-idf",
        "esp-idf",
        "idf_app_loop",
        10,
        4096,
        5,
    };
#endif
    return kConfig;
}

const product_composition::TargetProfile* esp32LvglRuntimeTargetProfile()
{
    return product_composition::findTargetProfile(esp32LvglRuntimeConfig().target_id);
}

bool hasEsp32LvglRuntimeTargetProfile()
{
    return esp32LvglRuntimeTargetProfile() != nullptr;
}

const product_composition::TargetUxBinding* esp32LvglRuntimeUxBinding()
{
    return product_composition::findTargetUxBinding(esp32LvglRuntimeConfig().target_id);
}

} // namespace trailmate::apps::esp32_lvgl

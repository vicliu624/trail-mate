#include "apps/esp_idf/runtime_config.h"

#include "sdkconfig.h"

namespace apps::esp_idf::runtime_config
{

const RuntimeConfig& get()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    static const RuntimeConfig kConfig = {
        "trail-mate-tab5",
        "Tab5",
        "tab5_app_loop",
        10,
        4096,
        5,
    };
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
#if defined(CONFIG_TRAIL_MATE_T_DISPLAY_P4_PANEL_RM69A10)
    static const RuntimeConfig kConfig = {
        "trail-mate-t-display-p4-amoled",
        "T-Display-P4 AMOLED",
        "t_display_p4_amoled_app_loop",
        10,
        4096,
        5,
    };
#else
    static const RuntimeConfig kConfig = {
        "trail-mate-t-display-p4-tft",
        "T-Display-P4 TFT",
        "t_display_p4_tft_app_loop",
        10,
        4096,
        5,
    };
#endif
#else
    static const RuntimeConfig kConfig = {
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

} // namespace apps::esp_idf::runtime_config

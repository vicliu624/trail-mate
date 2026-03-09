#include "apps/esp_idf/runtime_config.h"

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
    static const RuntimeConfig kConfig = {
        "trail-mate-t-display-p4",
        "T-Display-P4",
        "t_display_p4_app_loop",
        10,
        4096,
        5,
    };
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

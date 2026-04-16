#include "platform/esp/idf_common/bsp_runtime.h"

#include <cstddef>

#include "boards/t_display_p4/board_profile.h"
#include "boards/tab5/tab5_board.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
extern "C"
{
    esp_err_t bsp_sdcard_init(char* mount_point, size_t max_files);
    esp_err_t bsp_display_brightness_set(int brightness_percent);
    bool trail_mate_tab5_display_runtime_is_ready(void);
}
#endif

namespace platform::esp::idf_common::bsp_runtime
{
namespace
{

constexpr const char* kTag = "idf-bsp-runtime";
char kSdMountPoint[] = "/sdcard";
bool s_nvs_ready = false;
bool s_sdcard_ready = false;

} // namespace

bool ensure_nvs_ready()
{
    if (s_nvs_ready)
    {
        return true;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(kTag, "NVS init returned %s, erasing partition", esp_err_to_name(err));
        if (nvs_flash_erase() == ESP_OK)
        {
            err = nvs_flash_init();
        }
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(kTag, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return false;
    }

    s_nvs_ready = true;
    return true;
}

bool ensure_sdcard_ready()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    if (s_sdcard_ready)
    {
        return true;
    }
    if (!::boards::tab5::Tab5Board::hasSdCard())
    {
        return false;
    }

    esp_err_t err = bsp_sdcard_init(kSdMountPoint, 8);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE)
    {
        s_sdcard_ready = true;
        return true;
    }

    ESP_LOGW(kTag, "bsp_sdcard_init failed: %s", esp_err_to_name(err));
    return false;
#else
    return false;
#endif
}

bool sdcard_ready()
{
    return ensure_sdcard_ready();
}

const char* sdcard_mount_point()
{
    return kSdMountPoint;
}

bool display_ready()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return trail_mate_tab5_display_runtime_is_ready();
#else
    return false;
#endif
}

bool set_display_brightness(int brightness_percent)
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    if (display_ready() == false)
    {
        return false;
    }
    const esp_err_t err = bsp_display_brightness_set(brightness_percent);
    if (err == ESP_OK)
    {
        return true;
    }
    ESP_LOGW(kTag,
             "bsp_display_brightness_set(%d) failed: %s",
             brightness_percent,
             esp_err_to_name(err));
    return false;
#else
    (void)brightness_percent;
    return false;
#endif
}

bool wake_display()
{
    return set_display_brightness(default_awake_brightness_percent());
}

bool sleep_display()
{
    return set_display_brightness(0);
}

int default_awake_brightness_percent()
{
    return 100;
}

bool sdcard_capable()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return ::boards::tab5::Tab5Board::hasSdCard();
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return platform::esp::boards::t_display_p4::kBoardProfile.has_sdcard;
#else
    return false;
#endif
}

} // namespace platform::esp::idf_common::bsp_runtime

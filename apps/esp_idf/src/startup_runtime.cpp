#include "apps/esp_idf/startup_runtime.h"

#include "apps/esp_idf/app_runtime_access.h"
#include "apps/esp_idf/loop_runtime.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "board/BoardBase.h"
#include "boards/tab5/rtc_runtime.h"
#include "esp_log.h"
#include "platform/esp/boards/board_runtime.h"
#include "platform/esp/idf_common/bsp_runtime.h"
#include "platform/esp/idf_common/gps_runtime.h"
#include "platform/esp/idf_common/startup_support.h"
#include "platform/esp/idf_common/sx126x_radio.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/lora_runtime.h"
#include "platform/ui/screen_runtime.h"
#include "platform/ui/settings_store.h"
#include "ui/app_registry.h"
#include "ui/app_runtime.h"
#include "ui/startup_shell.h"

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
extern "C"
{
#include <stdint.h>

    bool bsp_display_lock(uint32_t timeout_ms);
    void bsp_display_unlock(void);
}
#endif

namespace apps::esp_idf::startup_runtime
{
namespace
{

bool lockUi(uint32_t timeout_ms)
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return bsp_display_lock(timeout_ms);
#else
    (void)timeout_ms;
    return true;
#endif
}

void unlockUi()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    bsp_display_unlock();
#endif
}

void applyPlatformRuntimeConfig(const RuntimeConfig& config)
{
    if (!app::hasAppFacade())
    {
        return;
    }

    const app::AppConfig& app_config = app::appFacade().getConfig();
    platform::ui::gps::set_collection_interval(app_config.gps_interval_ms);
    platform::ui::gps::set_power_strategy(app_config.gps_strategy);
    platform::ui::gps::set_gnss_config(app_config.gps_mode, app_config.gps_sat_mask);
    platform::ui::gps::set_nmea_config(app_config.privacy_nmea_output, app_config.privacy_nmea_sentence);
    platform::ui::gps::set_motion_idle_timeout(app_config.motion_config.idle_timeout_ms);
    platform::ui::gps::set_motion_sensor_id(app_config.motion_config.sensor_id);
    platform::esp::idf_common::gps_runtime::request_startup_probe();

    ESP_LOGI(config.log_tag,
             "GNSS runtime config applied: interval_ms=%lu mode=%u sat_mask=0x%02X strategy=%u nmea=%u/%u motion_idle_ms=%lu sensor=%u",
             static_cast<unsigned long>(app_config.gps_interval_ms),
             app_config.gps_mode,
             app_config.gps_sat_mask,
             app_config.gps_strategy,
             app_config.privacy_nmea_output,
             app_config.privacy_nmea_sentence,
             static_cast<unsigned long>(app_config.motion_config.idle_timeout_ms),
             app_config.motion_config.sensor_id);
}

void probeExternalModules(const RuntimeConfig& config)
{
    if (platform::ui::lora::is_supported())
    {
        auto& radio = platform::esp::idf_common::Sx126xRadio::instance();
        const bool online = platform::ui::lora::acquire();
        ESP_LOGI(config.log_tag,
                 "LoRa startup probe: supported=1 online=%d error=%s",
                 online ? 1 : 0,
                 radio.lastError());
        if (online)
        {
            platform::ui::lora::release();
        }
    }
}

ui::startup_shell::Hooks buildShellHooks()
{
    ui::startup_shell::Hooks hooks{};
    hooks.messaging = &app::messagingFacade();
    hooks.apps = ui::appCatalog();
    hooks.show_main_menu = menu_show;
    hooks.watch_face = ui::startup_shell::defaultWatchFaceHooks();
    hooks.set_max_brightness = []()
    {
        const int saved = platform::ui::settings_store::get_int("settings", "screen_brightness",
                                                                DEVICE_MAX_BRIGHTNESS_LEVEL);
        const int clamped =
            saved < DEVICE_MIN_BRIGHTNESS_LEVEL
                ? DEVICE_MIN_BRIGHTNESS_LEVEL
                : (saved > DEVICE_MAX_BRIGHTNESS_LEVEL ? DEVICE_MAX_BRIGHTNESS_LEVEL : saved);
        (void)platform::esp::idf_common::bsp_runtime::wake_display();
        platform::ui::device::set_screen_brightness(static_cast<uint8_t>(clamped));
    };
    return hooks;
}

} // namespace

extern "C" void trail_mate_idf_note_user_activity(void)
{
    platform::ui::screen::update_user_activity();
}

void run(const RuntimeConfig& config)
{
    constexpr bool waking_from_sleep = false;

    platform::esp::idf_common::startup_support::logStartupBanner(config.log_tag);
    (void)platform::esp::idf_common::bsp_runtime::ensure_nvs_ready();
    platform::esp::boards::initializeBoard(waking_from_sleep);
    platform::esp::boards::initializeDisplay();
    if (::boards::tab5::rtc_runtime::sync_system_time_from_hardware_rtc())
    {
        ESP_LOGI(config.log_tag, "Boot time restored from hardware RTC");
    }
    (void)platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready();

    ESP_LOGI(config.log_tag, "prepareBootUi begin waking=%d", waking_from_sleep ? 1 : 0);
    if (lockUi(1000))
    {
        ui::startup_shell::prepareBootUi(waking_from_sleep);
        unlockUi();
        ESP_LOGI(config.log_tag, "prepareBootUi complete");
    }
    else
    {
        ESP_LOGW(config.log_tag, "prepareBootUi failed to acquire LVGL lock");
    }

    app_runtime_access::initialize(config);
    applyPlatformRuntimeConfig(config);
    probeExternalModules(config);
    const ui::startup_shell::Hooks shell_hooks = buildShellHooks();

    const size_t app_count = ui::catalogCount(shell_hooks.apps);
    ESP_LOGI(config.log_tag, "initializeShell begin app_count=%u", static_cast<unsigned>(app_count));
    if (lockUi(1000))
    {
        ui::startup_shell::initializeShell(shell_hooks);
        unlockUi();
        ESP_LOGI(config.log_tag, "initializeShell complete");
    }
    else
    {
        ESP_LOGW(config.log_tag, "initializeShell failed to acquire LVGL lock");
    }

    const auto& runtime_status = app_runtime_access::status();
    ESP_LOGI(config.log_tag,
             "%s runtime status handles=%d lifecycle=%d bound=%d",
             config.target_name,
             runtime_status.board_handles_ready ? 1 : 0,
             runtime_status.lifecycle_ready ? 1 : 0,
             runtime_status.app_context_bound ? 1 : 0);

    loop_runtime::start(config);

    ESP_LOGI(config.log_tag, "finalizeStartup begin waking=%d", waking_from_sleep ? 1 : 0);
    if (lockUi(1000))
    {
        ui::startup_shell::finalizeStartup(waking_from_sleep);
        unlockUi();
        ESP_LOGI(config.log_tag, "finalizeStartup complete");
    }
    else
    {
        ESP_LOGW(config.log_tag, "finalizeStartup failed to acquire LVGL lock");
    }

    ESP_LOGI(config.log_tag, "%s startup runtime initialized", config.target_name);
}

} // namespace apps::esp_idf::startup_runtime

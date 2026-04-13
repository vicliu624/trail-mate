#include "apps/esp_pio/startup_runtime.h"

#include <Arduino.h>

#include "app/app_facade_access.h"
#include "apps/esp_pio/app_runtime_access.h"
#include "board/BoardBase.h"
#include "display/DisplayConfig.h"
#include "platform/esp/arduino_common/display_runtime.h"
#include "platform/esp/arduino_common/startup_support.h"
#include "platform/ui/settings_store.h"
#include "ui/app_registry.h"
#include "ui/app_runtime.h"
#include "ui/startup_shell.h"

namespace
{

void initializeShell()
{
    ui::startup_shell::Hooks hooks{};
    hooks.messaging = &app::messagingFacade();
    hooks.apps = ui::appCatalog();
    hooks.set_max_brightness = []()
    {
        const int saved = platform::ui::settings_store::get_int("settings", "screen_brightness",
                                                                DEVICE_MAX_BRIGHTNESS_LEVEL);
        const int clamped =
            saved < DEVICE_MIN_BRIGHTNESS_LEVEL
                ? DEVICE_MIN_BRIGHTNESS_LEVEL
                : (saved > DEVICE_MAX_BRIGHTNESS_LEVEL ? DEVICE_MAX_BRIGHTNESS_LEVEL : saved);
        board.setBrightness(static_cast<uint8_t>(clamped));
    };
    hooks.show_main_menu = menu_show;
    hooks.watch_face = ui::startup_shell::defaultWatchFaceHooks();
    ui::startup_shell::initializeShell(hooks);
}

void finishStartup(bool waking_from_sleep)
{
    ui::startup_shell::finalizeStartup(waking_from_sleep);
    if (waking_from_sleep)
    {
        Serial.printf("[Setup] Updated user activity after waking from sleep\n");
    }
}

} // namespace

namespace apps::esp_pio::startup_runtime
{

void run()
{
    Serial.begin(115200);
    delay(100);
    Serial.printf("\n\n[Setup] ===== SYSTEM STARTUP =====\n");
    Serial.printf("[Setup] Serial initialized at 115200 baud\n");

    platform::esp::arduino_common::startup_support::initializeClockProviders();

    const esp_sleep_wakeup_cause_t wakeup_reason =
        platform::esp::arduino_common::startup_support::detectWakeupCause();
    const bool waking_from_sleep =
        platform::esp::arduino_common::startup_support::isWakingFromSleep(wakeup_reason);
    if (waking_from_sleep)
    {
        Serial.printf("[Setup] Wakeup cause: %d\n", wakeup_reason);
    }

    platform::esp::arduino_common::startup_support::initializeBoard(waking_from_sleep);
    Serial.printf("[Setup] heap=%u psram=%u\n", ESP.getFreeHeap(), ESP.getFreePsram());
    Serial.println("[Setup] LVGL init begin");
    platform::esp::arduino_common::display_runtime::initialize();
    Serial.println("[Setup] LVGL init done");

    ui::startup_shell::prepareBootUi(waking_from_sleep);

    bool use_mock = false;
    if (apps::esp_pio::app_runtime_access::initialize(use_mock))
    {
        const auto& runtime_status = apps::esp_pio::app_runtime_access::status();
        Serial.printf("[Setup] AppContext initialized handles=%d bound=%d tasks=%d\n",
                      runtime_status.board_handles_ready ? 1 : 0,
                      runtime_status.app_context_bound ? 1 : 0,
                      runtime_status.background_tasks_started ? 1 : 0);
    }
    else
    {
        const auto& runtime_status = apps::esp_pio::app_runtime_access::status();
        Serial.printf("[Setup] WARNING: AppContext init failed handles=%d bound=%d tasks=%d\n",
                      runtime_status.board_handles_ready ? 1 : 0,
                      runtime_status.app_context_bound ? 1 : 0,
                      runtime_status.background_tasks_started ? 1 : 0);
    }

    initializeShell();
    finishStartup(waking_from_sleep);
}

} // namespace apps::esp_pio::startup_runtime

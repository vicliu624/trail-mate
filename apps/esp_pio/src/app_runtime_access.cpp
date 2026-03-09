#include "apps/esp_pio/app_runtime_access.h"

#include <Arduino.h>

#include "apps/esp_pio/app_context.h"
#include "platform/esp/arduino_common/app_runtime_bootstrap_support.h"
#include "platform/esp/arduino_common/app_runtime_support.h"
#include "platform/esp/boards/board_runtime.h"

namespace apps::esp_pio::app_runtime_access
{
namespace
{

Status s_status{};

} // namespace

bool initialize(bool use_mock)
{
    if (s_status.initialized)
    {
        return s_status.app_context_bound;
    }

    s_status = Status{};
    s_status.initialized = true;

    const auto handles = platform::esp::boards::resolveAppContextInitHandles();
    app::AppContext& app_context = app::AppContext::getInstance();
    platform::esp::arduino_common::AppContextBootstrapResult bootstrap_result{};
    s_status.board_handles_ready = handles.isValid();
    if (!s_status.board_handles_ready)
    {
        Serial.printf("[APP] ERROR: board runtime returned invalid AppContext handles\n");
        return false;
    }

    if (!platform::esp::arduino_common::bootstrapAppContext(app_context, handles, use_mock, &bootstrap_result))
    {
        return false;
    }

    s_status.app_context_bound = bootstrap_result.app_context_bound;

    switch (bootstrap_result.background_tasks)
    {
    case platform::esp::arduino_common::BackgroundTaskStartResult::NotSupported:
        Serial.printf("[APP] WARNING: Board type not supported for LoRa tasks\n");
        return true;

    case platform::esp::arduino_common::BackgroundTaskStartResult::Failed:
        Serial.printf("[APP] WARNING: Failed to start LoRa tasks\n");
        return true;

    case platform::esp::arduino_common::BackgroundTaskStartResult::Started:
        s_status.background_tasks_started = true;
        Serial.printf("[APP] LoRa tasks started\n");
        return true;
    }

    return true;
}

void tick()
{
    platform::esp::arduino_common::tickBoundLifecycle();
}

const Status& status()
{
    return s_status;
}

} // namespace apps::esp_pio::app_runtime_access

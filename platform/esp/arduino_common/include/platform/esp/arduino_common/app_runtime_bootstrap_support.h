#pragma once

#include "platform/esp/arduino_common/app_runtime_support.h"
#include "platform/esp/boards/board_runtime.h"

namespace app
{
class AppContext;
}

namespace platform::esp::arduino_common
{

struct AppContextBootstrapResult
{
    bool app_context_bound = false;
    BackgroundTaskStartResult background_tasks = BackgroundTaskStartResult::NotSupported;
};

bool bootstrapAppContext(app::AppContext& app_context,
                         const platform::esp::boards::AppContextInitHandles& handles,
                         bool use_mock,
                         AppContextBootstrapResult* out_result = nullptr);

} // namespace platform::esp::arduino_common

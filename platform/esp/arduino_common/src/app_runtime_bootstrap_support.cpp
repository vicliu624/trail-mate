#include "platform/esp/arduino_common/app_runtime_bootstrap_support.h"

#include "app/app_context.h"
#include "app/app_facade_access.h"
#include "ble/ble_manager.h"
#include "platform/esp/arduino_common/app_context_platform_bindings.h"
#include "platform/esp/arduino_common/app_event_runtime_support.h"
#include "platform/esp/arduino_common/memory_diag.h"
#include "ui/ui_boot.h"

namespace platform::esp::arduino_common
{

bool bootstrapAppContext(app::AppContext& app_context,
                         const platform::esp::boards::AppContextInitHandles& handles,
                         bool use_mock,
                         AppContextBootstrapResult* out_result)
{
    AppContextBootstrapResult result{};

    app_context.configurePlatformBindings(platform::esp::arduino_common::makeAppContextPlatformBindings());

    if (!app_context.init(*handles.board, handles.lora_board, handles.gps_board, handles.motion_board, use_mock))
    {
        app::unbindAppFacade();
        if (out_result)
        {
            *out_result = result;
        }
        return false;
    }

    memory_diag::logHeapSnapshot("bootstrap.after_app_context_init");
    app::bindAppFacade(app_context);
    ui::boot::set_log_line("Starting BLE...");
    app_context.attachBleManager(platform::esp::arduino_common::createBleManager(app_context));
    memory_diag::logHeapSnapshot("bootstrap.after_ble_manager");
    app_context.attachEventRuntimeHooks(platform::esp::arduino_common::makeAppEventRuntimeHooks());
    memory_diag::logHeapSnapshot("bootstrap.after_event_hooks");
    result.app_context_bound = true;
    ui::boot::set_log_line("Starting radio tasks...");
    result.background_tasks = platform::esp::arduino_common::startBackgroundTasks(handles.lora_board,
                                                                                  app_context.getMeshAdapter());
    memory_diag::logHeapSnapshot("bootstrap.after_background_tasks");
    memory_diag::logTaskSnapshot("bootstrap.after_background_tasks");

    if (out_result)
    {
        *out_result = result;
    }
    return true;
}

} // namespace platform::esp::arduino_common

#pragma once

#include <cstddef>
#include <memory>

class LoraBoard;

namespace chat
{
class IMeshAdapter;
}

namespace app
{
class IAppBleFacade;
class IAppFacade;
}

namespace ble
{
class BleManager;
}

namespace sys
{
struct Event;
}

namespace platform::esp::arduino_common
{

enum class BackgroundTaskStartResult
{
    NotSupported,
    Failed,
    Started,
};

std::unique_ptr<ble::BleManager> createBleManager(app::IAppBleFacade& app_facade);
BackgroundTaskStartResult startBackgroundTasks(LoraBoard* board, chat::IMeshAdapter* adapter);
void tickBoundLifecycle(std::size_t max_events = 32);
void tickRuntime(app::IAppFacade& app_context);
void updateCoreServices(app::IAppFacade& app_context);
bool dispatchEvent(app::IAppFacade& app_context, sys::Event* event);

} // namespace platform::esp::arduino_common

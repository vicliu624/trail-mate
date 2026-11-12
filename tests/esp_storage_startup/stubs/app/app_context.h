#pragma once
#include "ble/ble_manager.h"
#include <memory>
class BoardBase;
class LoraBoard;
class GpsBoard;
class MotionBoard;
namespace chat
{
class IMeshAdapter;
}
namespace platform::esp::arduino_common
{
struct AppContextPlatformBindings;
struct AppEventRuntimeHooks;
} // namespace platform::esp::arduino_common
namespace app
{
class IAppBleFacade
{
};
class AppContext : public IAppBleFacade
{
  public:
    void configurePlatformBindings(const platform::esp::arduino_common::AppContextPlatformBindings&) {}
    bool init(BoardBase&, LoraBoard*, GpsBoard*, MotionBoard*, bool);
    void attachBleManager(std::unique_ptr<ble::BleManager> manager) { manager_ = std::move(manager); }
    void attachEventRuntimeHooks(const platform::esp::arduino_common::AppEventRuntimeHooks&) {}
    chat::IMeshAdapter* getMeshAdapter() { return nullptr; }
    void broadcastNodeInfo() {}

  private:
    std::unique_ptr<ble::BleManager> manager_;
};
} // namespace app

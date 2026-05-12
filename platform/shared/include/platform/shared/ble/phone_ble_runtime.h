#pragma once

#include <cstdint>

namespace platform::shared::ble_bridge
{

class IPhoneBleRuntime
{
  public:
    virtual ~IPhoneBleRuntime() = default;

    virtual bool isPhoneBleConnected() const = 0;
    virtual uint32_t pendingPhoneBlePasskey() const = 0;
    virtual void requestPhoneHighThroughputConnection() = 0;
    virtual void requestPhoneLowerPowerConnection() = 0;
    virtual void onPhoneBluetoothConfigChanged() {}
    virtual void onPhoneModuleConfigChanged() {}
};

} // namespace platform::shared::ble_bridge

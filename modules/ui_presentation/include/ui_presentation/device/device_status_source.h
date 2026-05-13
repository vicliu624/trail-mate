#pragma once

#include "ui_presentation/device/device_status_snapshot.h"

namespace ui::device
{

class IDeviceStatusSource
{
  public:
    virtual ~IDeviceStatusSource() = default;

    virtual bool buildDeviceStatusSnapshot(DeviceStatusSnapshot& out) const = 0;
};

} // namespace ui::device

#pragma once

#include "ui_presentation/device/device_status_source.h"

namespace ui::device
{

class DeviceStatusModel
{
  public:
    explicit DeviceStatusModel(IDeviceStatusSource& source);

    DeviceStatusSnapshot snapshot() const;

  private:
    IDeviceStatusSource& source_;
};

} // namespace ui::device

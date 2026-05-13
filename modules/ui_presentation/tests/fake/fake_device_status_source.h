#pragma once

#include "ui_presentation/device/device_status_source.h"

namespace ui::tests
{

class FakeDeviceStatusSource final : public ui::device::IDeviceStatusSource
{
  public:
    bool buildDeviceStatusSnapshot(ui::device::DeviceStatusSnapshot& out) const override
    {
        if (!available)
        {
            return false;
        }
        out = snapshot_value;
        return true;
    }

    bool available = true;
    ui::device::DeviceStatusSnapshot snapshot_value{};
};

} // namespace ui::tests

#include "ui_presentation/device/device_status_model.h"

namespace ui::device
{

DeviceStatusModel::DeviceStatusModel(IDeviceStatusSource& source)
    : source_(source)
{
}

DeviceStatusSnapshot DeviceStatusModel::snapshot() const
{
    DeviceStatusSnapshot out{};
    if (!source_.buildDeviceStatusSnapshot(out))
    {
        out.header.valid = false;
    }
    return out;
}

} // namespace ui::device

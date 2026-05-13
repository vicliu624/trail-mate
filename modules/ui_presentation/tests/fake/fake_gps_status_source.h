#pragma once

#include "ui_presentation/gps/gps_status_source.h"

namespace ui::tests
{

class FakeGpsStatusSource final : public ui::gps::IGpsStatusSource
{
  public:
    bool buildGpsStatusSnapshot(ui::gps::GpsStatusSnapshot& out) const override
    {
        if (!available)
        {
            return false;
        }
        out = snapshot_value;
        return true;
    }

    bool available = true;
    ui::gps::GpsStatusSnapshot snapshot_value{};
};

} // namespace ui::tests

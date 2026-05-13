#pragma once

#include "ui_presentation/gps/gps_status_snapshot.h"

namespace ui::gps
{

class IGpsStatusSource
{
  public:
    virtual ~IGpsStatusSource() = default;

    virtual bool buildGpsStatusSnapshot(GpsStatusSnapshot& out) const = 0;
};

} // namespace ui::gps

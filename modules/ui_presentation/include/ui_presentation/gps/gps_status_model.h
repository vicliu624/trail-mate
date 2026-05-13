#pragma once

#include "ui_presentation/gps/gps_status_source.h"

namespace ui::gps
{

class GpsStatusModel
{
  public:
    explicit GpsStatusModel(IGpsStatusSource& source);

    GpsStatusSnapshot snapshot() const;

  private:
    IGpsStatusSource& source_;
};

} // namespace ui::gps

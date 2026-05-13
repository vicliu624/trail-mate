#pragma once

#include "ui_presentation/gps/gps_status_source.h"

namespace ui::presentation_sources
{

class LegacyGpsStatusSource final : public ui::gps::IGpsStatusSource
{
  public:
    bool buildGpsStatusSnapshot(ui::gps::GpsStatusSnapshot& out) const override;
};

LegacyGpsStatusSource& legacy_gps_status_source();

} // namespace ui::presentation_sources

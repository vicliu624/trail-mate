#pragma once

#include "ui_presentation/map/map_overlay_snapshot.h"

#include <cstdint>

namespace ui
{
namespace map_overlay
{

class MapOverlayProjector
{
  public:
    bool projectCurrentPosition(double lat,
                                double lon,
                                bool valid,
                                ::ui::map::MapOverlaySnapshot& out) const;

    bool projectTeamMember(uint32_t node_id,
                           const char* label,
                           double lat,
                           double lon,
                           bool valid,
                           ::ui::map::MapOverlaySnapshot& out) const;

  private:
    bool append(::ui::map::MapOverlaySnapshot& out,
                const ::ui::map::MapOverlayItem& item) const;
};

} // namespace map_overlay
} // namespace ui

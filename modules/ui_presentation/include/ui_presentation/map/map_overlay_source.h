#pragma once

#include "ui_presentation/map/map_overlay_snapshot.h"

namespace ui
{
namespace map
{

class IMapOverlayPresentationSource
{
  public:
    virtual ~IMapOverlayPresentationSource() = default;

    virtual bool buildMapOverlaySnapshot(MapOverlaySnapshot& out) const = 0;
};

} // namespace map
} // namespace ui

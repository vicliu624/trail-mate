#pragma once

#include "ui_map_runtime/map_overlay_projection_adapter.h"
#include "ui_presentation/map/map_overlay_source.h"

namespace ui
{
namespace map_overlay
{

class MapOverlaySnapshotSource final
    : public ::ui::map::IMapOverlayPresentationSource
{
  public:
    MapOverlaySnapshotSource(IMapOverlayGpsSource* gps,
                             IMapOverlayTeamSource* team);

    bool buildMapOverlaySnapshot(
        ::ui::map::MapOverlaySnapshot& out) const override;

  private:
    IMapOverlayGpsSource* gps_ = nullptr;
    IMapOverlayTeamSource* team_ = nullptr;
    MapOverlayProjectionAdapter adapter_{};
};

} // namespace map_overlay
} // namespace ui

#include "ui_map_runtime/map_overlay_snapshot_source.h"

namespace ui
{
namespace map_overlay
{

MapOverlaySnapshotSource::MapOverlaySnapshotSource(IMapOverlayGpsSource* gps,
                                                   IMapOverlayTeamSource* team)
    : gps_(gps),
      team_(team)
{
}

bool MapOverlaySnapshotSource::buildMapOverlaySnapshot(
    ::ui::map::MapOverlaySnapshot& out) const
{
    return adapter_.project(gps_, team_, out);
}

} // namespace map_overlay
} // namespace ui

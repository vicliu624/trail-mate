#include "ui_map_runtime/map_overlay_projection_adapter.h"

namespace ui
{
namespace map_overlay
{
namespace
{

constexpr std::size_t kMaxTeamPoints = 16;

} // namespace

bool MapOverlayProjectionAdapter::project(
    const IMapOverlayGpsSource* gps,
    const IMapOverlayTeamSource* team,
    ::ui::map::MapOverlaySnapshot& out) const
{
    out = ::ui::map::MapOverlaySnapshot{};
    out.header.valid = true;
    out.header.version = 1;

    if (gps != nullptr)
    {
        double lat = 0.0;
        double lon = 0.0;
        bool valid = false;
        if (gps->currentFix(lat, lon, valid))
        {
            (void)projector_.projectCurrentPosition(lat, lon, valid, out);
        }
    }

    if (team != nullptr)
    {
        IMapOverlayTeamSource::TeamPoint points[kMaxTeamPoints]{};
        const std::size_t count = team->latestTeamPoints(points, kMaxTeamPoints);
        for (std::size_t i = 0; i < count && i < kMaxTeamPoints; ++i)
        {
            (void)projector_.projectTeamMember(points[i].node_id,
                                               points[i].label,
                                               points[i].lat,
                                               points[i].lon,
                                               points[i].valid,
                                               out);
        }
        if (count > kMaxTeamPoints)
        {
            out.truncated = true;
        }
    }

    return true;
}

} // namespace map_overlay
} // namespace ui

#include "ui/presentation_sources/legacy_map_overlay_source.h"

namespace ui
{
namespace presentation_sources
{
namespace
{

constexpr std::size_t kMaxLegacyTeamPoints = 16;

} // namespace

LegacyMapOverlaySource::LegacyMapOverlaySource(IMapOverlayGpsSource* gps,
                                               IMapOverlayTeamSource* team)
    : gps_(gps),
      team_(team)
{
}

bool LegacyMapOverlaySource::buildMapOverlaySnapshot(
    ::ui::map::MapOverlaySnapshot& out) const
{
    out = ::ui::map::MapOverlaySnapshot{};
    out.header.valid = true;
    out.header.version = 1;

    if (gps_ != nullptr)
    {
        double lat = 0.0;
        double lon = 0.0;
        bool valid = false;
        if (gps_->currentFix(lat, lon, valid))
        {
            (void)projector_.projectCurrentPosition(lat, lon, valid, out);
        }
    }

    if (team_ != nullptr)
    {
        IMapOverlayTeamSource::TeamPoint points[kMaxLegacyTeamPoints]{};
        const std::size_t count = team_->latestTeamPoints(points, kMaxLegacyTeamPoints);
        for (std::size_t i = 0; i < count && i < kMaxLegacyTeamPoints; ++i)
        {
            (void)projector_.projectTeamMember(points[i].node_id,
                                               points[i].label,
                                               points[i].lat,
                                               points[i].lon,
                                               points[i].valid,
                                               out);
        }
        if (count > kMaxLegacyTeamPoints)
        {
            out.truncated = true;
        }
    }

    return true;
}

} // namespace presentation_sources
} // namespace ui

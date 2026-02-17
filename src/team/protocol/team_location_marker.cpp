/**
 * @file team_location_marker.cpp
 * @brief Team location marker icon mapping.
 */

#include "team_location_marker.h"

namespace team::proto
{

bool team_location_marker_icon_is_valid(uint8_t icon_id)
{
    return icon_id >= kTeamLocationMarkerIconMin &&
           icon_id <= kTeamLocationMarkerIconMax;
}

const char* team_location_marker_icon_name(uint8_t icon_id)
{
    switch (static_cast<TeamLocationMarkerIcon>(icon_id))
    {
    case TeamLocationMarkerIcon::AreaCleared:
        return "Area Cleared";
    case TeamLocationMarkerIcon::BaseCamp:
        return "Base Camp";
    case TeamLocationMarkerIcon::GoodFind:
        return "Good Find";
    case TeamLocationMarkerIcon::Rally:
        return "Rally";
    case TeamLocationMarkerIcon::Sos:
        return "SOS";
    default:
        return "Location";
    }
}

} // namespace team::proto

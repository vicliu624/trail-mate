/**
 * @file team_location_marker.h
 * @brief Team location marker icon mapping.
 */

#pragma once

#include <cstdint>

namespace team::proto
{

enum class TeamLocationMarkerIcon : uint8_t
{
    None = 0,
    AreaCleared = 1,
    BaseCamp = 2,
    GoodFind = 3,
    Rally = 4,
    Sos = 5
};

constexpr uint8_t kTeamLocationMarkerIconMin =
    static_cast<uint8_t>(TeamLocationMarkerIcon::AreaCleared);
constexpr uint8_t kTeamLocationMarkerIconMax =
    static_cast<uint8_t>(TeamLocationMarkerIcon::Sos);

bool team_location_marker_icon_is_valid(uint8_t icon_id);
const char* team_location_marker_icon_name(uint8_t icon_id);

} // namespace team::proto

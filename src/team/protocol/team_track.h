/**
 * @file team_track.h
 * @brief Team track protocol payloads
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace team::proto
{

constexpr uint8_t kTeamTrackVersion = 1;
constexpr size_t kTeamTrackMaxPoints = 20;

struct TeamTrackPoint
{
    int32_t lat_e7 = 0;
    int32_t lon_e7 = 0;
};

struct TeamTrackMessage
{
    uint8_t version = kTeamTrackVersion;
    uint32_t start_ts = 0;
    uint16_t interval_s = 0;
    uint32_t valid_mask = 0;
    std::vector<TeamTrackPoint> points;
};

bool encodeTeamTrackMessage(const TeamTrackMessage& msg, std::vector<uint8_t>& out);
bool decodeTeamTrackMessage(const uint8_t* data, size_t len, TeamTrackMessage* out);

} // namespace team::proto

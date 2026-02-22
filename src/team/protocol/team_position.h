/**
 * @file team_position.h
 * @brief Team position protocol payloads
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace team::proto
{

constexpr uint8_t kTeamPositionVersion = 1;

enum TeamPositionFlags : uint16_t
{
    kTeamPosHasAltitude = 1u << 0,
    kTeamPosHasSpeed = 1u << 1,
    kTeamPosHasCourse = 1u << 2,
    kTeamPosHasSatellites = 1u << 3
};

struct TeamPositionMessage
{
    uint8_t version = kTeamPositionVersion;
    uint16_t flags = 0;
    int32_t lat_e7 = 0;
    int32_t lon_e7 = 0;
    int16_t alt_m = 0;
    uint16_t speed_dmps = 0;
    uint16_t course_cdeg = 0;
    uint8_t sats_in_view = 0;
    uint32_t ts = 0;
};

inline bool teamPositionHasAltitude(const TeamPositionMessage& msg)
{
    return (msg.flags & kTeamPosHasAltitude) != 0;
}

inline bool teamPositionHasSpeed(const TeamPositionMessage& msg)
{
    return (msg.flags & kTeamPosHasSpeed) != 0;
}

inline bool teamPositionHasCourse(const TeamPositionMessage& msg)
{
    return (msg.flags & kTeamPosHasCourse) != 0;
}

inline bool teamPositionHasSatellites(const TeamPositionMessage& msg)
{
    return (msg.flags & kTeamPosHasSatellites) != 0;
}

bool encodeTeamPositionMessage(const TeamPositionMessage& msg, std::vector<uint8_t>& out);
bool decodeTeamPositionMessage(const uint8_t* data, size_t len, TeamPositionMessage* out);

} // namespace team::proto

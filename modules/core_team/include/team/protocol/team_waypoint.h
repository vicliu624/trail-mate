/**
 * @file team_waypoint.h
 * @brief Team waypoint protocol payloads
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace team::proto
{

constexpr uint8_t kTeamWaypointVersion = 1;
constexpr size_t kTeamWaypointNameMaxLen = 30;
constexpr size_t kTeamWaypointDescMaxLen = 100;
constexpr size_t kTeamWaypointIconMaxLen = 24;

enum TeamWaypointFlags : uint16_t
{
    kTeamWaypointHasLocation = 1u << 0
};

struct TeamWaypointMessage
{
    uint8_t version = kTeamWaypointVersion;
    uint16_t flags = kTeamWaypointHasLocation;
    uint32_t id = 0;
    int32_t lat_e7 = 0;
    int32_t lon_e7 = 0;
    uint32_t expire_ts = 0;
    uint32_t locked_to = 0;
    std::string name;
    std::string description;
    std::string icon;
};

inline bool teamWaypointHasLocation(const TeamWaypointMessage& msg)
{
    return (msg.flags & kTeamWaypointHasLocation) != 0;
}

bool encodeTeamWaypointMessage(const TeamWaypointMessage& msg, std::vector<uint8_t>& out);
bool decodeTeamWaypointMessage(const uint8_t* data, size_t len, TeamWaypointMessage* out);

} // namespace team::proto

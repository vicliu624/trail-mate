#pragma once

#include <stdint.h>

namespace ui::team_actions
{

enum class TeamActionKind : uint8_t
{
    Text,
    LocationMarker,
    Command,
};

enum class TeamCommandKind : uint8_t
{
    Unknown,
    RallyPoint,
    MoveTo,
    Hold,
    Custom,
};

struct TeamLocationMarkerRequest
{
    bool use_current_location = false;
    double lat = 0.0;
    double lon = 0.0;
    bool has_altitude = false;
    double altitude_m = 0.0;
    float accuracy_m = 0.0f;
    uint32_t timestamp = 0;
    uint8_t marker_icon = 0;
    const char* label = nullptr;
};

struct TeamLocationSnapshot
{
    bool valid = false;
    double lat = 0.0;
    double lon = 0.0;
    bool has_altitude = false;
    double altitude_m = 0.0;
    float accuracy_m = 0.0f;
    uint32_t timestamp = 0;
};

struct TeamCommandRequest
{
    TeamCommandKind kind = TeamCommandKind::Unknown;
    double lat = 0.0;
    double lon = 0.0;
    uint16_t radius_m = 0;
    uint8_t priority = 0;
    const char* payload = nullptr;
};

struct TeamActionRequest
{
    TeamActionKind kind = TeamActionKind::Text;
    const char* text = nullptr;
    TeamLocationMarkerRequest location;
    TeamCommandRequest command;
};

} // namespace ui::team_actions

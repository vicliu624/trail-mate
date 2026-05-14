#pragma once

#include "ui_presentation/common/fixed_text.h"

#include <stdint.h>

namespace ui::team_presentation
{

enum class TeamRichPayloadKind : uint8_t
{
    None,
    Text,
    Location,
    Command,
    Unsupported,
};

enum class TeamCommandDisplayKind : uint8_t
{
    Unknown,
    MoveTo,
    RallyPoint,
    Hold,
    Help,
    CheckIn,
    Custom,
};

struct TeamLocationDisplay
{
    double lat = 0.0;
    double lon = 0.0;
    bool has_altitude = false;
    float altitude_m = 0.0f;
    uint8_t marker_icon = 0;
};

struct TeamCommandDisplay
{
    TeamCommandDisplayKind kind = TeamCommandDisplayKind::Unknown;
    double lat = 0.0;
    double lon = 0.0;
    float radius_m = 0.0f;
    uint8_t priority = 0;
};

struct TeamRichPayloadDisplay
{
    TeamRichPayloadKind kind = TeamRichPayloadKind::None;

    ui::FixedText<32> title;
    ui::FixedText<96> summary;
    ui::FixedText<32> badge;

    TeamLocationDisplay location;
    TeamCommandDisplay command;
};

} // namespace ui::team_presentation

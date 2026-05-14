#include "ui/team_presentation/team_rich_payload_projector.h"

#include "team/protocol/team_chat.h"
#include "team/protocol/team_location_marker.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace ui::team_presentation
{

namespace
{

void copyTextPayloadSummary(TeamRichPayloadDisplay& out,
                            const uint8_t* data,
                            std::size_t size)
{
    char buffer[96]{};
    if (data == nullptr || size == 0)
    {
        ui::copyText(out.summary, "");
        return;
    }

    const std::size_t max_copy = sizeof(buffer) - 1U;
    const bool truncated = size > max_copy;
    std::size_t copy_len = truncated ? sizeof(buffer) - 4U : size;
    if (copy_len > 0)
    {
        std::memcpy(buffer, data, copy_len);
    }
    if (truncated)
    {
        buffer[copy_len] = '.';
        buffer[copy_len + 1U] = '.';
        buffer[copy_len + 2U] = '.';
        buffer[copy_len + 3U] = '\0';
    }
    else
    {
        buffer[copy_len] = '\0';
    }
    ui::copyText(out.summary, buffer);
}

const char* teamCommandName(team::proto::TeamCommandType type)
{
    switch (type)
    {
    case team::proto::TeamCommandType::RallyTo:
        return "RallyTo";
    case team::proto::TeamCommandType::MoveTo:
        return "MoveTo";
    case team::proto::TeamCommandType::Hold:
        return "Hold";
    }
    return "Command";
}

TeamCommandDisplayKind toDisplayKind(team::proto::TeamCommandType type)
{
    switch (type)
    {
    case team::proto::TeamCommandType::RallyTo:
        return TeamCommandDisplayKind::RallyPoint;
    case team::proto::TeamCommandType::MoveTo:
        return TeamCommandDisplayKind::MoveTo;
    case team::proto::TeamCommandType::Hold:
        return TeamCommandDisplayKind::Hold;
    }
    return TeamCommandDisplayKind::Unknown;
}

void formatCoordinate(char* out, std::size_t out_len, double lat, double lon)
{
    if (out == nullptr || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%.5f, %.5f", lat, lon);
}

void copySummary(TeamRichPayloadDisplay& out, const char* value)
{
    ui::copyText(out.summary, value);
}

} // namespace

bool TeamRichPayloadProjector::project(
    const ::team::ui::TeamChatLogEntry& entry,
    TeamRichPayloadDisplay& out) const
{
    out = TeamRichPayloadDisplay{};

    if (entry.type == team::proto::TeamChatType::Text)
    {
        out.kind = TeamRichPayloadKind::Text;
        ui::copyText(out.title, "Message");
        ui::copyText(out.badge, "Text");
        copyTextPayloadSummary(out,
                               entry.payload.empty() ? nullptr
                                                     : entry.payload.data(),
                               entry.payload.size());
        return true;
    }

    if (entry.type == team::proto::TeamChatType::Location)
    {
        team::proto::TeamChatLocation loc;
        out.kind = TeamRichPayloadKind::Location;
        ui::copyText(out.title, "Location");
        ui::copyText(out.badge, "Location");

        if (!team::proto::decodeTeamChatLocation(entry.payload.data(),
                                                 entry.payload.size(),
                                                 &loc))
        {
            copySummary(out, "Location");
            return false;
        }

        out.location.lat = static_cast<double>(loc.lat_e7) / 10000000.0;
        out.location.lon = static_cast<double>(loc.lon_e7) / 10000000.0;
        out.location.has_altitude = loc.alt_m != 0;
        out.location.altitude_m = static_cast<float>(loc.alt_m);
        out.location.marker_icon = loc.source;

        char coord[64]{};
        formatCoordinate(coord,
                         sizeof(coord),
                         out.location.lat,
                         out.location.lon);
        const bool marker =
            team::proto::team_location_marker_icon_is_valid(loc.source);
        const char* marker_name =
            team::proto::team_location_marker_icon_name(loc.source);

        char buffer[160]{};
        if (marker)
        {
            ui::copyText(out.title, marker_name);
            ui::copyText(out.badge, "Marker");
            std::snprintf(buffer,
                          sizeof(buffer),
                          "%s: %s",
                          marker_name,
                          coord);
        }
        else if (!loc.label.empty())
        {
            std::snprintf(buffer,
                          sizeof(buffer),
                          "Location: %s %s",
                          loc.label.c_str(),
                          coord);
        }
        else
        {
            std::snprintf(buffer,
                          sizeof(buffer),
                          "Location: %s",
                          coord);
        }
        copySummary(out, buffer);
        return true;
    }

    if (entry.type == team::proto::TeamChatType::Command)
    {
        team::proto::TeamChatCommand command;
        out.kind = TeamRichPayloadKind::Command;
        ui::copyText(out.title, "Command");
        ui::copyText(out.badge, "Command");

        if (!team::proto::decodeTeamChatCommand(entry.payload.data(),
                                                entry.payload.size(),
                                                &command))
        {
            copySummary(out, "Command");
            return false;
        }

        const char* name = teamCommandName(command.cmd_type);
        out.command.kind = toDisplayKind(command.cmd_type);
        out.command.lat = static_cast<double>(command.lat_e7) / 10000000.0;
        out.command.lon = static_cast<double>(command.lon_e7) / 10000000.0;
        out.command.radius_m = static_cast<float>(command.radius_m);
        out.command.priority = command.priority;

        char buffer[160]{};
        if (command.lat_e7 != 0 || command.lon_e7 != 0)
        {
            char coord[64]{};
            formatCoordinate(coord,
                             sizeof(coord),
                             out.command.lat,
                             out.command.lon);
            if (!command.note.empty())
            {
                std::snprintf(buffer,
                              sizeof(buffer),
                              "Command: %s %s %s",
                              name,
                              coord,
                              command.note.c_str());
            }
            else
            {
                std::snprintf(buffer,
                              sizeof(buffer),
                              "Command: %s %s",
                              name,
                              coord);
            }
        }
        else if (!command.note.empty())
        {
            std::snprintf(buffer,
                          sizeof(buffer),
                          "Command: %s %s",
                          name,
                          command.note.c_str());
        }
        else
        {
            std::snprintf(buffer, sizeof(buffer), "Command: %s", name);
        }
        copySummary(out, buffer);
        return true;
    }

    out.kind = TeamRichPayloadKind::Unsupported;
    ui::copyText(out.title, "Unsupported");
    ui::copyText(out.badge, "Unsupported");
    copySummary(out, "Unsupported team payload");
    return false;
}

} // namespace ui::team_presentation

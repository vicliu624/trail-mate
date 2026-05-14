#include "platform/ui/team_ui_store_runtime.h"
#include "team/protocol/team_chat.h"
#include "team/protocol/team_location_marker.h"
#include "ui/team_presentation/team_rich_payload_projector.h"

#include <cassert>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace
{

team::ui::TeamChatLogEntry makeEntry(team::proto::TeamChatType type,
                                     std::vector<uint8_t> payload)
{
    team::ui::TeamChatLogEntry entry;
    entry.type = type;
    entry.payload = std::move(payload);
    return entry;
}

bool contains(const ui::FixedText<96>& text, const char* needle)
{
    return std::strstr(text.c_str(), needle) != nullptr;
}

} // namespace

int main()
{
    ui::team_presentation::TeamRichPayloadProjector projector;
    ui::team_presentation::TeamRichPayloadDisplay display;

    std::vector<uint8_t> text_payload(180, 'a');
    assert(projector.project(makeEntry(team::proto::TeamChatType::Text,
                                       text_payload),
                             display));
    assert(display.kind == ui::team_presentation::TeamRichPayloadKind::Text);
    assert(std::strcmp(display.badge.c_str(), "Text") == 0);
    assert(std::strlen(display.summary.c_str()) == 95);

    team::proto::TeamChatLocation loc;
    loc.lat_e7 = 312345678;
    loc.lon_e7 = 1219876543;
    loc.alt_m = 42;
    loc.source = static_cast<uint8_t>(team::proto::TeamLocationMarkerIcon::Rally);
    std::vector<uint8_t> location_payload;
    assert(team::proto::encodeTeamChatLocation(loc, location_payload));
    assert(projector.project(makeEntry(team::proto::TeamChatType::Location,
                                       location_payload),
                             display));
    assert(display.kind == ui::team_presentation::TeamRichPayloadKind::Location);
    assert(std::strcmp(display.badge.c_str(), "Marker") == 0);
    assert(contains(display.summary, "Rally"));
    assert(contains(display.summary, "31.23457"));
    assert(display.location.has_altitude);
    assert(display.location.marker_icon ==
           static_cast<uint8_t>(team::proto::TeamLocationMarkerIcon::Rally));

    loc = team::proto::TeamChatLocation{};
    loc.lat_e7 = -123456789;
    loc.lon_e7 = 987654321;
    loc.label = "Trailhead";
    location_payload.clear();
    assert(team::proto::encodeTeamChatLocation(loc, location_payload));
    assert(projector.project(makeEntry(team::proto::TeamChatType::Location,
                                       location_payload),
                             display));
    assert(display.kind == ui::team_presentation::TeamRichPayloadKind::Location);
    assert(std::strcmp(display.badge.c_str(), "Location") == 0);
    assert(contains(display.summary, "Trailhead"));
    assert(contains(display.summary, "-12.34568"));

    team::proto::TeamChatCommand command;
    command.cmd_type = team::proto::TeamCommandType::Hold;
    command.note = "wait";
    std::vector<uint8_t> command_payload;
    assert(team::proto::encodeTeamChatCommand(command, command_payload));
    assert(projector.project(makeEntry(team::proto::TeamChatType::Command,
                                       command_payload),
                             display));
    assert(display.kind == ui::team_presentation::TeamRichPayloadKind::Command);
    assert(display.command.kind == ui::team_presentation::TeamCommandDisplayKind::Hold);
    assert(contains(display.summary, "Command: Hold"));
    assert(contains(display.summary, "wait"));

    command = team::proto::TeamChatCommand{};
    command.cmd_type = team::proto::TeamCommandType::MoveTo;
    command.lat_e7 = 111111111;
    command.lon_e7 = -222222222;
    command.radius_m = 25;
    command.priority = 3;
    command_payload.clear();
    assert(team::proto::encodeTeamChatCommand(command, command_payload));
    assert(projector.project(makeEntry(team::proto::TeamChatType::Command,
                                       command_payload),
                             display));
    assert(display.command.kind == ui::team_presentation::TeamCommandDisplayKind::MoveTo);
    assert(display.command.radius_m == 25.0f);
    assert(display.command.priority == 3);
    assert(contains(display.summary, "11.11111"));
    assert(contains(display.summary, "-22.22222"));

    assert(!projector.project(makeEntry(team::proto::TeamChatType::Location,
                                        {0x01, 0x02}),
                              display));
    assert(display.kind == ui::team_presentation::TeamRichPayloadKind::Location);
    assert(std::strcmp(display.summary.c_str(), "Location") == 0);

    assert(!projector.project(makeEntry(team::proto::TeamChatType::Command,
                                        {0x03}),
                              display));
    assert(display.kind == ui::team_presentation::TeamRichPayloadKind::Command);
    assert(std::strcmp(display.summary.c_str(), "Command") == 0);

    assert(!projector.project(makeEntry(
                                  static_cast<team::proto::TeamChatType>(99),
                                  {}),
                              display));
    assert(display.kind == ui::team_presentation::TeamRichPayloadKind::Unsupported);
    assert(std::strcmp(display.summary.c_str(),
                       "Unsupported team payload") == 0);

    return 0;
}

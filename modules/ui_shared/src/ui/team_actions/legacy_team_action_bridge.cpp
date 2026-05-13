#include "ui/team_actions/legacy_team_action_bridge.h"

#include "sys/clock.h"
#include "team/protocol/team_chat.h"
#include "team/protocol/team_location_marker.h"

#include <cmath>
#include <string>
#include <vector>

namespace ui::team_actions
{
namespace
{

constexpr uint32_t kMinValidEpochSeconds = 1577836800U; // 2020-01-01
constexpr uint32_t kTeamComposeMsgIdStart = 1U;

uint32_t currentTimestamp()
{
    uint32_t ts = sys::epoch_seconds_now();
    if (ts < kMinValidEpochSeconds)
    {
        ts = static_cast<uint32_t>(sys::millis_now() / 1000U);
    }
    return ts;
}

bool validCoordinate(double lat, double lon)
{
    return std::isfinite(lat) && std::isfinite(lon) &&
           lat >= -90.0 && lat <= 90.0 &&
           lon >= -180.0 && lon <= 180.0;
}

int32_t coordinateToE7(double coordinate)
{
    return static_cast<int32_t>(std::lround(coordinate * 1e7));
}

int16_t clampAltitude(double altitude_m)
{
    if (altitude_m > 32767.0)
    {
        return 32767;
    }
    if (altitude_m < -32768.0)
    {
        return -32768;
    }
    return static_cast<int16_t>(std::lround(altitude_m));
}

uint16_t clampAccuracy(float accuracy_m)
{
    if (!std::isfinite(accuracy_m) || accuracy_m <= 0.0f)
    {
        return 0;
    }
    if (accuracy_m > 65535.0f)
    {
        return 65535;
    }
    return static_cast<uint16_t>(std::lround(accuracy_m));
}

team::proto::TeamCommandType toProtoCommand(TeamCommandKind kind)
{
    switch (kind)
    {
    case TeamCommandKind::RallyPoint:
        return team::proto::TeamCommandType::RallyTo;
    case TeamCommandKind::MoveTo:
        return team::proto::TeamCommandType::MoveTo;
    case TeamCommandKind::Hold:
        return team::proto::TeamCommandType::Hold;
    case TeamCommandKind::Custom:
    case TeamCommandKind::Unknown:
        break;
    }
    return team::proto::TeamCommandType::RallyTo;
}

} // namespace

LegacyTeamActionBridge::LegacyTeamActionBridge(
    ::team::ui::ITeamUiStore& team_store,
    ::ui::presentation_sources::ITeamChatCommandPort* command_port,
    ITeamLocationSource* location_source,
    uint8_t team_channel_raw)
    : team_store_(team_store),
      command_port_(command_port),
      location_source_(location_source),
      team_channel_raw_(team_channel_raw)
{
}

ui::UiActionResult LegacyTeamActionBridge::sendTeamAction(
    const TeamActionRequest& request)
{
    switch (request.kind)
    {
    case TeamActionKind::Text:
        return sendText(request);
    case TeamActionKind::LocationMarker:
        return sendLocationMarker(request);
    case TeamActionKind::Command:
        return sendCommand(request);
    }
    return ui::UiActionResult::fail(ui::UiActionFailure::Unsupported);
}

ui::UiActionResult LegacyTeamActionBridge::sendText(
    const TeamActionRequest& request)
{
    if (request.text == nullptr || request.text[0] == '\0')
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
    }

    ::team::ui::TeamUiSnapshot snap;
    if (!team_store_.load(snap) || !snap.has_team_id || !snap.has_team_psk ||
        command_port_ == nullptr)
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::NotReady);
    }
    if (!command_port_->setKeysFromPsk(snap.team_id,
                                       snap.security_round,
                                       snap.team_psk.data(),
                                       snap.team_psk.size()))
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::NotReady);
    }

    const uint32_t ts = currentTimestamp();
    const std::string text(request.text);

    team::proto::TeamChatMessage message;
    message.header.type = team::proto::TeamChatType::Text;
    message.header.ts = ts;
    message.header.msg_id = next_message_id_++;
    if (next_message_id_ == 0)
    {
        next_message_id_ = kTeamComposeMsgIdStart;
    }
    message.payload.assign(text.begin(), text.end());

    if (!command_port_->sendTeamChat(message, team_channel_raw_))
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::Rejected);
    }

    (void)::team::ui::team_ui_chatlog_append_structured(
        snap.team_id,
        0,
        false,
        ts,
        team::proto::TeamChatType::Text,
        message.payload);
    return ui::UiActionResult::success();
}

ui::UiActionResult LegacyTeamActionBridge::sendLocationMarker(
    const TeamActionRequest& request)
{
    TeamLocationMarkerRequest location = request.location;
    if (!team::proto::team_location_marker_icon_is_valid(location.marker_icon))
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
    }

    if (location.use_current_location)
    {
        if (location_source_ == nullptr)
        {
            return ui::UiActionResult::fail(ui::UiActionFailure::NotReady);
        }

        TeamLocationSnapshot snapshot;
        if (!location_source_->currentTeamLocation(snapshot) || !snapshot.valid)
        {
            return ui::UiActionResult::fail(ui::UiActionFailure::NotReady);
        }

        location.lat = snapshot.lat;
        location.lon = snapshot.lon;
        location.has_altitude = snapshot.has_altitude;
        location.altitude_m = snapshot.altitude_m;
        if (location.accuracy_m <= 0.0f)
        {
            location.accuracy_m = snapshot.accuracy_m;
        }
        if (location.timestamp == 0)
        {
            location.timestamp = snapshot.timestamp;
        }
    }

    if (!validCoordinate(location.lat, location.lon))
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
    }

    ::team::ui::TeamUiSnapshot snap;
    if (!team_store_.load(snap) || !snap.has_team_id || !snap.has_team_psk ||
        command_port_ == nullptr)
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::NotReady);
    }
    if (!command_port_->setKeysFromPsk(snap.team_id,
                                       snap.security_round,
                                       snap.team_psk.data(),
                                       snap.team_psk.size()))
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::NotReady);
    }

    uint32_t ts = location.timestamp;
    if (ts == 0)
    {
        ts = currentTimestamp();
    }

    team::proto::TeamChatLocation loc;
    loc.lat_e7 = coordinateToE7(location.lat);
    loc.lon_e7 = coordinateToE7(location.lon);
    loc.alt_m = location.has_altitude ? clampAltitude(location.altitude_m) : 0;
    loc.acc_m = clampAccuracy(location.accuracy_m);
    loc.ts = ts;
    loc.source = location.marker_icon;
    loc.label = location.label != nullptr
                    ? std::string(location.label)
                    : std::string(team::proto::team_location_marker_icon_name(
                          location.marker_icon));

    std::vector<uint8_t> payload;
    if (!team::proto::encodeTeamChatLocation(loc, payload))
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::Rejected);
    }

    team::proto::TeamChatMessage message;
    message.header.type = team::proto::TeamChatType::Location;
    message.header.ts = ts;
    message.header.msg_id = next_message_id_++;
    if (next_message_id_ == 0)
    {
        next_message_id_ = kTeamComposeMsgIdStart;
    }
    message.payload = payload;

    if (!command_port_->sendTeamChat(message, team_channel_raw_))
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::Rejected);
    }

    (void)::team::ui::team_ui_chatlog_append_structured(
        snap.team_id,
        0,
        false,
        ts,
        team::proto::TeamChatType::Location,
        payload);
    return ui::UiActionResult::success();
}

ui::UiActionResult LegacyTeamActionBridge::sendCommand(
    const TeamActionRequest& request)
{
    const auto& command = request.command;
    if (command.kind == TeamCommandKind::Unknown)
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
    }
    if (command.kind == TeamCommandKind::Custom)
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::Unsupported);
    }
    if (!validCoordinate(command.lat, command.lon))
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
    }

    ::team::ui::TeamUiSnapshot snap;
    if (!team_store_.load(snap) || !snap.has_team_id || !snap.has_team_psk ||
        command_port_ == nullptr)
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::NotReady);
    }
    if (!command_port_->setKeysFromPsk(snap.team_id,
                                       snap.security_round,
                                       snap.team_psk.data(),
                                       snap.team_psk.size()))
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::NotReady);
    }

    const uint32_t ts = currentTimestamp();

    team::proto::TeamChatCommand cmd;
    cmd.cmd_type = toProtoCommand(command.kind);
    cmd.lat_e7 = coordinateToE7(command.lat);
    cmd.lon_e7 = coordinateToE7(command.lon);
    cmd.radius_m = command.radius_m;
    cmd.priority = command.priority;
    if (command.payload != nullptr)
    {
        cmd.note = command.payload;
    }

    std::vector<uint8_t> payload;
    if (!team::proto::encodeTeamChatCommand(cmd, payload))
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::Rejected);
    }

    team::proto::TeamChatMessage message;
    message.header.type = team::proto::TeamChatType::Command;
    message.header.ts = ts;
    message.header.msg_id = next_message_id_++;
    if (next_message_id_ == 0)
    {
        next_message_id_ = kTeamComposeMsgIdStart;
    }
    message.payload = payload;

    if (!command_port_->sendTeamChat(message, team_channel_raw_))
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::Rejected);
    }

    (void)::team::ui::team_ui_chatlog_append_structured(
        snap.team_id,
        0,
        false,
        ts,
        team::proto::TeamChatType::Command,
        payload);
    return ui::UiActionResult::success();
}

} // namespace ui::team_actions

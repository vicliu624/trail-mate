#include "ui/team_actions/legacy_team_action_bridge.h"

#include "team/protocol/team_chat.h"
#include "team/protocol/team_location_marker.h"

#include <cassert>
#include <vector>

namespace
{

class FakeTeamCommandPort final
    : public ui::presentation_sources::ITeamChatCommandPort
{
  public:
    bool setKeysFromPsk(const ::team::TeamId& team_id,
                        uint32_t key_id,
                        const uint8_t* psk,
                        size_t psk_len) override
    {
        ++set_keys_count;
        last_team_id = team_id;
        last_key_id = key_id;
        last_psk_len = psk_len;
        return set_keys_ok && psk != nullptr && psk_len > 0;
    }

    bool sendTeamChat(const ::team::proto::TeamChatMessage& message,
                      uint8_t team_channel_raw) override
    {
        ++send_count;
        last_message = message;
        last_channel_raw = team_channel_raw;
        return send_ok;
    }

    int set_keys_count = 0;
    int send_count = 0;
    bool set_keys_ok = true;
    bool send_ok = true;
    ::team::TeamId last_team_id{};
    uint32_t last_key_id = 0;
    size_t last_psk_len = 0;
    uint8_t last_channel_raw = 0;
    ::team::proto::TeamChatMessage last_message{};
};

class FakeTeamLocationSource final : public ui::team_actions::ITeamLocationSource
{
  public:
    bool currentTeamLocation(ui::team_actions::TeamLocationSnapshot& out) override
    {
        ++read_count;
        out = snapshot;
        return read_ok;
    }

    int read_count = 0;
    bool read_ok = true;
    ui::team_actions::TeamLocationSnapshot snapshot{};
};

team::ui::TeamUiSnapshot readySnapshot()
{
    team::ui::TeamUiSnapshot snapshot;
    snapshot.in_team = true;
    snapshot.team_id = {1, 2, 3, 4, 5, 6, 7, 8};
    snapshot.has_team_id = true;
    snapshot.security_round = 42;
    snapshot.has_team_psk = true;
    for (size_t i = 0; i < snapshot.team_psk.size(); ++i)
    {
        snapshot.team_psk[i] = static_cast<uint8_t>(i + 1);
    }
    return snapshot;
}

} // namespace

int main()
{
    team::ui::TeamUiMemoryStore store;
    team::ui::team_ui_set_store(&store);
    store.clear();

    FakeTeamCommandPort port;
    FakeTeamLocationSource location_source;
    location_source.snapshot.valid = true;
    location_source.snapshot.lat = 30.2672;
    location_source.snapshot.lon = -97.7431;
    location_source.snapshot.has_altitude = true;
    location_source.snapshot.altitude_m = 153.2;
    location_source.snapshot.accuracy_m = 6.0f;
    location_source.snapshot.timestamp = 12345;

    ui::team_actions::LegacyTeamActionBridge bridge(store,
                                                    &port,
                                                    &location_source);

    ui::team_actions::TeamActionRequest location;
    location.kind = ui::team_actions::TeamActionKind::LocationMarker;
    location.location.lat = 31.2304;
    location.location.lon = 121.4737;
    location.location.marker_icon =
        static_cast<uint8_t>(team::proto::TeamLocationMarkerIcon::Rally);
    assert(bridge.sendTeamAction(location).failure == ui::UiActionFailure::NotReady);

    store.save(readySnapshot());

    location.location.marker_icon = 99;
    assert(bridge.sendTeamAction(location).failure ==
           ui::UiActionFailure::InvalidInput);

    location.location.marker_icon =
        static_cast<uint8_t>(team::proto::TeamLocationMarkerIcon::Rally);
    location.location.has_altitude = true;
    location.location.altitude_m = 123.4;
    location.location.accuracy_m = 7.0f;
    const auto location_result = bridge.sendTeamAction(location);
    assert(location_result.ok);
    assert(port.set_keys_count == 1);
    assert(port.send_count == 1);
    assert(port.last_message.header.type == team::proto::TeamChatType::Location);
    assert(port.last_channel_raw == 2);

    team::proto::TeamChatLocation decoded_location;
    assert(team::proto::decodeTeamChatLocation(
        port.last_message.payload.data(),
        port.last_message.payload.size(),
        &decoded_location));
    assert(decoded_location.source == location.location.marker_icon);
    assert(decoded_location.lat_e7 == 312304000);
    assert(decoded_location.lon_e7 == 1214737000);
    assert(decoded_location.alt_m == 123);
    assert(decoded_location.acc_m == 7);

    ui::team_actions::TeamActionRequest current_location;
    current_location.kind = ui::team_actions::TeamActionKind::LocationMarker;
    current_location.location.use_current_location = true;
    current_location.location.marker_icon =
        static_cast<uint8_t>(team::proto::TeamLocationMarkerIcon::GoodFind);
    const auto current_location_result = bridge.sendTeamAction(current_location);
    assert(current_location_result.ok);
    assert(location_source.read_count == 1);
    assert(port.last_message.header.type == team::proto::TeamChatType::Location);
    assert(team::proto::decodeTeamChatLocation(
        port.last_message.payload.data(),
        port.last_message.payload.size(),
        &decoded_location));
    assert(decoded_location.lat_e7 == 302672000);
    assert(decoded_location.lon_e7 == -977431000);
    assert(decoded_location.alt_m == 153);
    assert(decoded_location.acc_m == 6);
    assert(decoded_location.ts == 12345);

    location_source.snapshot.valid = false;
    assert(bridge.sendTeamAction(current_location).failure ==
           ui::UiActionFailure::NotReady);
    location_source.snapshot.valid = true;

    ui::team_actions::LegacyTeamActionBridge bridge_without_location_source(
        store,
        &port);
    assert(bridge_without_location_source.sendTeamAction(current_location).failure ==
           ui::UiActionFailure::NotReady);

    std::vector<team::ui::TeamChatLogEntry> log;
    assert(team::ui::team_ui_chatlog_load_recent(readySnapshot().team_id, 4, log));
    assert(!log.empty());
    assert(log.back().type == team::proto::TeamChatType::Location);

    ui::team_actions::TeamActionRequest text;
    text.kind = ui::team_actions::TeamActionKind::Text;
    text.text = "team hello";
    const auto text_result = bridge.sendTeamAction(text);
    assert(text_result.ok);
    assert(port.last_message.header.type == team::proto::TeamChatType::Text);

    ui::team_actions::TeamActionRequest command;
    command.kind = ui::team_actions::TeamActionKind::Command;
    assert(bridge.sendTeamAction(command).failure ==
           ui::UiActionFailure::InvalidInput);

    command.command.kind = ui::team_actions::TeamCommandKind::Custom;
    assert(bridge.sendTeamAction(command).failure ==
           ui::UiActionFailure::Unsupported);

    command.command.kind = ui::team_actions::TeamCommandKind::MoveTo;
    command.command.lat = 31.2304;
    command.command.lon = 121.4737;
    command.command.payload = "move";
    const auto command_result = bridge.sendTeamAction(command);
    assert(command_result.ok);
    assert(port.last_message.header.type == team::proto::TeamChatType::Command);

    team::proto::TeamChatCommand decoded_command;
    assert(team::proto::decodeTeamChatCommand(
        port.last_message.payload.data(),
        port.last_message.payload.size(),
        &decoded_command));
    assert(decoded_command.cmd_type == team::proto::TeamCommandType::MoveTo);
    assert(decoded_command.note == "move");

    store.clear();
    team::ui::team_ui_set_store(nullptr);
    return 0;
}

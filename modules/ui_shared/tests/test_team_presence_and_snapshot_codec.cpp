#include "ui/team_persistence/team_ui_snapshot_codec.h"
#include "ui/team_presence/team_presence_model.h"

#include <cassert>
#include <cstring>
#include <vector>

namespace
{

team::TeamId testTeamId()
{
    team::TeamId id{};
    for (std::size_t i = 0; i < id.size(); ++i)
    {
        id[i] = static_cast<uint8_t>(0xA0 + i);
    }
    return id;
}

void appendU8(std::vector<uint8_t>& out, uint8_t value)
{
    out.push_back(value);
}

void appendU16(std::vector<uint8_t>& out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void appendU32(std::vector<uint8_t>& out, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
    {
        out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
    }
}

void appendU64(std::vector<uint8_t>& out, uint64_t value)
{
    for (int i = 0; i < 8; ++i)
    {
        out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
    }
}

uint64_t teamIdToU64(const team::TeamId& id)
{
    uint64_t value = 0;
    for (std::size_t i = 0; i < id.size(); ++i)
    {
        value |= static_cast<uint64_t>(id[i]) << (8 * i);
    }
    return value;
}

std::vector<uint8_t> buildV1SnapshotBytes()
{
    std::vector<uint8_t> out;
    out.insert(out.end(), {'T', 'M', 'S', '1'});
    appendU8(out, ui::team_persistence::kTeamUiSnapshotVersionV1);
    appendU8(out, 0x01);
    appendU16(out, 0);
    appendU32(out, 777);
    appendU64(out, teamIdToU64(testTeamId()));
    appendU32(out, 42);
    appendU32(out, 7);
    appendU32(out, 0);
    appendU32(out, 0x11111111);
    appendU8(out, 2);
    appendU8(out, 0);
    appendU8(out, 0);
    appendU8(out, 0);
    appendU16(out, 1);
    appendU16(out, 0);
    appendU32(out, 0x11111111);
    appendU8(out, 2);
    appendU8(out, 1);
    appendU16(out, 3);
    out.insert(out.end(), {'A', 'd', 'a'});
    appendU32(out, 5);
    return out;
}

team::ui::TeamUiSnapshot makeV2Snapshot()
{
    team::ui::TeamUiSnapshot snapshot;
    snapshot.in_team = true;
    snapshot.has_team_id = true;
    snapshot.team_id = testTeamId();
    snapshot.self_is_leader = true;
    snapshot.security_round = 9;
    snapshot.last_event_seq = 33;
    snapshot.team_chat_unread = 4;

    team::ui::TeamMemberUi self;
    self.node_id = 0;
    self.name = "You";
    self.leader = true;
    self.last_seen_s = 1000;
    snapshot.members.push_back(self);

    team::ui::TeamMemberUi remote;
    remote.node_id = 0x12345678;
    remote.name = "Remote teammate with an overlong name";
    remote.last_seen_s = 997;
    snapshot.members.push_back(remote);
    return snapshot;
}

void testPresenceModel()
{
    std::vector<team::ui::TeamMemberUi> members;
    team::ui::TeamMemberUi recent;
    recent.node_id = 1;
    recent.last_seen_s = 980;
    members.push_back(recent);

    team::ui::TeamMemberUi stale;
    stale.node_id = 2;
    stale.last_seen_s = 850;
    members.push_back(stale);

    team::ui::TeamMemberUi unknown;
    unknown.node_id = 3;
    unknown.last_seen_s = 0;
    members.push_back(unknown);

    assert(ui::team_presence::refreshTeamMemberPresence(members, 1000) == 1);
    assert(members[0].online);
    assert(!members[1].online);
    assert(!members[2].online);
    assert(ui::team_presence::isTeamMemberOnline(1000, 1005));
    assert(!ui::team_presence::isTeamMemberOnline(1000, 0));

    const uint32_t seen =
        ui::team_presence::normalizeSeenSeconds(0, 1001);
    const auto touch =
        ui::team_presence::touchTeamMember(members, 2, seen);
    assert(touch.valid);
    assert(!touch.created);
    assert(touch.index == 1);
    assert(members[1].last_seen_s == 1001);
    assert(ui::team_presence::refreshTeamMemberPresence(members, 1001) == 2);

    const auto created =
        ui::team_presence::touchTeamMember(members, 4, 1002);
    assert(created.valid);
    assert(created.created);
    assert(members[created.index].node_id == 4);
}

void testSnapshotV2RoundTrip()
{
    const auto input = makeV2Snapshot();
    std::vector<uint8_t> bytes;
    assert(ui::team_persistence::encodeTeamUiSnapshot(input, 1234, bytes));

    team::ui::TeamUiSnapshot decoded;
    assert(ui::team_persistence::decodeTeamUiSnapshot(
        bytes.data(), bytes.size(), decoded));

    assert(decoded.in_team);
    assert(decoded.has_team_id);
    assert(decoded.team_id == input.team_id);
    assert(decoded.self_is_leader);
    assert(decoded.security_round == 9);
    assert(decoded.last_event_seq == 33);
    assert(decoded.last_update_s == 1234);
    assert(decoded.team_chat_unread == 4);
    assert(decoded.members.size() == 2);
    assert(decoded.members[0].node_id == 0);
    assert(decoded.members[0].leader);
    assert(decoded.members[0].last_seen_s == 1000);
    assert(decoded.members[1].node_id == 0x12345678);
    assert(decoded.members[1].last_seen_s == 997);
    assert(decoded.members[1].name.size() == 24);
}

void testSnapshotV1Compatibility()
{
    const std::vector<uint8_t> bytes = buildV1SnapshotBytes();
    team::ui::TeamUiSnapshot decoded;
    assert(ui::team_persistence::decodeTeamUiSnapshot(
        bytes.data(), bytes.size(), decoded));

    assert(decoded.in_team);
    assert(decoded.has_team_id);
    assert(decoded.team_id == testTeamId());
    assert(decoded.self_is_leader);
    assert(decoded.security_round == 42);
    assert(decoded.last_event_seq == 7);
    assert(decoded.team_chat_unread == 5);
    assert(decoded.members.size() == 1);
    assert(decoded.members[0].node_id == 0x11111111);
    assert(decoded.members[0].leader);
    assert(decoded.members[0].name == "Ada");
    assert(decoded.members[0].last_seen_s == 0);
}

} // namespace

int main()
{
    testPresenceModel();
    testSnapshotV2RoundTrip();
    testSnapshotV1Compatibility();
    return 0;
}

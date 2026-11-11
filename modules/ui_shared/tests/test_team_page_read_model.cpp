#include "ui/screens/team/team_page_read_model.h"

#include "platform/ui/team_ui_snapshot_store.h"

#include <cassert>

namespace
{

team::TeamId testTeamId()
{
    team::TeamId id{};
    id[0] = 0xAB;
    id[1] = 0xCD;
    return id;
}

team::ui::TeamPageReadModelInput makeInput()
{
    team::ui::TeamPageReadModelInput input;
    input.in_team = true;
    input.self_is_leader = true;
    input.has_team_id = true;
    input.team_id = testTeamId();
    input.security_round = 7;
    input.last_update_s = 990;
    input.has_team_psk = true;

    team::ui::TeamMemberUi leader;
    leader.node_id = 0x11111111;
    leader.name = "Ada";
    leader.leader = true;
    leader.last_seen_s = 980;
    leader.color_index = 1;
    input.members.push_back(leader);

    team::ui::TeamMemberUi stale;
    stale.node_id = 0x22222222;
    stale.name = "Ben";
    stale.last_seen_s = 600;
    stale.color_index = 2;
    input.members.push_back(stale);

    team::ui::TeamMemberUi unknown;
    unknown.node_id = 0x33333333;
    unknown.name = "Cy";
    unknown.last_seen_s = 0;
    unknown.color_index = 3;
    input.members.push_back(unknown);

    input.selected_member_index = 1;
    return input;
}

void testSummaryProjection()
{
    const team::ui::TeamPageReadModel model(1000);
    const auto input = makeInput();
    const auto summary = model.buildSummary(input);

    assert(summary.in_team);
    assert(summary.self_is_leader);
    assert(summary.team_name == "TEAM-ABCD");
    assert(summary.member_count == 3);
    assert(summary.has_security_round);
    assert(summary.security_round == 7);
    assert(summary.last_update.kind == team::ui::TeamRelativeTimeKind::SecondsAgo);
    assert(summary.last_update.value == 10);
}

void testRowsDoNotMutatePresence()
{
    const team::ui::TeamPageReadModel model(1000);
    auto input = makeInput();
    input.members[0].online = false;

    const auto rows = model.buildMemberRows(input.members);

    assert(rows.size() == 3);
    assert(rows[0].source_index == 0);
    assert(rows[0].name == "1111");
    assert(rows[0].last_seen.kind == team::ui::TeamRelativeTimeKind::MinutesAgo);
    assert(rows[0].last_seen.value == 0);
    assert(rows[0].leader);
    assert(rows[1].source_index == 1);
    assert(rows[1].name == "2222");
    assert(rows[1].last_seen.kind == team::ui::TeamRelativeTimeKind::MinutesAgo);
    assert(rows[1].last_seen.value == 6);
    assert(rows[2].name == "3333");
    assert(input.members[0].online == false);
}

void testMemberDetailProjection()
{
    const team::ui::TeamPageReadModel model(1000);
    auto input = makeInput();

    const auto detail = model.buildSelectedMember(input);

    assert(detail.valid);
    assert(detail.member.name == "2222");
    assert(detail.last_seen.kind == team::ui::TeamRelativeTimeKind::MinutesAgo);
    assert(detail.last_seen.value == 6);
    assert(detail.management_actions_enabled);

    input.waiting_new_keys = true;
    assert(!model.buildSelectedMember(input).management_actions_enabled);

    input.selected_member_index = 42;
    assert(!model.buildSelectedMember(input).valid);
}

void testJoinPendingProjection()
{
    const team::ui::TeamPageReadModel model(1000);
    auto input = makeInput();
    input.pairing_role = team::TeamPairingRole::Leader;
    input.pairing_state = team::TeamPairingState::WaitingKey;
    input.pairing_team_name = "Trail";

    const auto pending = model.buildJoinPending(input);

    assert(std::string(pending.title) == "Pairing (Leader)");
    assert(pending.show_leader_members);
    assert(pending.target_team_name == "Trail");
    assert(std::string(pending.state_line) == "Waiting for keys...");

    input.pairing_role = team::TeamPairingRole::Member;
    input.pairing_state = team::TeamPairingState::Failed;
    const auto member_pending = model.buildJoinPending(input);
    assert(std::string(member_pending.title) == "Pairing (Member)");
    assert(!member_pending.show_leader_members);
    assert(std::string(member_pending.state_line) == "Pairing failed");
}

void testSnapshotInputProjection()
{
    team::ui::TeamUiSnapshot snapshot;
    snapshot.in_team = true;
    snapshot.has_team_id = true;
    snapshot.team_id = testTeamId();
    snapshot.team_name = "Trail";
    snapshot.has_team_psk = true;

    const auto input = team::ui::TeamPageReadModel::inputFromSnapshot(snapshot);

    assert(input.in_team);
    assert(input.has_team_id);
    assert(input.team_id == snapshot.team_id);
    assert(input.team_name == "Trail");
    assert(input.has_team_psk);
    assert(!input.waiting_new_keys);
}

void testRelativeTimesDoNotMixEpochWithProcessUptime()
{
    const team::ui::TeamPageReadModel uptime_model(1000);
    assert(uptime_model.buildLastSeen(1794369005).kind ==
           team::ui::TeamRelativeTimeKind::Unknown);
    assert(uptime_model.buildLastUpdate(1794369005).kind ==
           team::ui::TeamRelativeTimeKind::Unknown);

    const team::ui::TeamPageReadModel epoch_model(1794382005);
    const auto leader = epoch_model.buildLastSeen(1794369005);
    assert(leader.kind == team::ui::TeamRelativeTimeKind::HoursAgo);
    assert(leader.value == 3);
    assert(epoch_model.buildLastSeen(30739).kind ==
           team::ui::TeamRelativeTimeKind::Unknown);

    auto input = makeInput();
    input.members[0].node_id = 0;
    input.members[0].name = "You";
    input.members[0].last_seen_s = 30739;
    input.selected_member_index = 0;
    const auto self = epoch_model.buildSelectedMember(input);
    assert(self.member.self);
    assert(self.last_seen.kind == team::ui::TeamRelativeTimeKind::MinutesAgo);
    assert(self.last_seen.value == 0);
    assert(input.members[0].last_seen_s == 30739);
}

} // namespace

int main()
{
    testSummaryProjection();
    testRowsDoNotMutatePresence();
    testMemberDetailProjection();
    testJoinPendingProjection();
    testSnapshotInputProjection();
    testRelativeTimesDoNotMixEpochWithProcessUptime();
    return 0;
}

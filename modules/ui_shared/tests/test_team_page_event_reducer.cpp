#include "ui/screens/team/team_page_event_reducer.h"

#include <cassert>
#include <cstdio>
#include <string>

namespace
{

class TestMemberNameResolver final : public team::ui::ITeamPageMemberNameResolver
{
  public:
    std::string resolveMemberName(uint32_t node_id) const override
    {
        char label[5]{};
        std::snprintf(label,
                      sizeof(label),
                      "%04lX",
                      static_cast<unsigned long>(node_id & 0xFFFFU));
        return std::string(label);
    }
};

class PrefixMemberNameResolver final : public team::ui::ITeamPageMemberNameResolver
{
  public:
    std::string resolveMemberName(uint32_t node_id) const override
    {
        char label[6]{};
        std::snprintf(label,
                      sizeof(label),
                      "N%04lX",
                      static_cast<unsigned long>(node_id & 0xFFFFU));
        return std::string(label);
    }
};

const TestMemberNameResolver& testNames()
{
    static TestMemberNameResolver names;
    return names;
}

team::TeamId testTeamId()
{
    team::TeamId id{};
    id[0] = 0xAB;
    id[1] = 0xCD;
    return id;
}

team::ui::TeamPageEventReducer makeReducer()
{
    team::ui::TeamPageEventContext context;
    context.now_s = 1000;
    context.self_node_id = 0x11111111;
    return team::ui::TeamPageEventReducer(context, testNames());
}

team::ui::TeamPageEventReducer makeReducer(
    const team::ui::ITeamPageMemberNameResolver& names)
{
    team::ui::TeamPageEventContext context;
    context.now_s = 1000;
    context.self_node_id = 0x11111111;
    return team::ui::TeamPageEventReducer(context, names);
}

team::TeamEventContext makeEventContext()
{
    team::TeamEventContext context;
    context.team_id = testTeamId();
    context.key_id = 7;
    context.from = 0x22222222;
    context.timestamp = 990;
    return context;
}

void testStatusRostersPreservePresenceAndLeader()
{
    auto reducer = makeReducer();
    team::ui::TeamPageEventState state;
    state.security_round = 6;

    team::TeamStatusEvent event;
    event.ctx = makeEventContext();
    event.msg.key_id = 7;
    event.msg.leader_id = 0x11111111;
    event.msg.members = {0x11111111, 0x22222222};
    event.msg.has_members = true;

    const auto effects = reducer.reduceStatus(state, event);

    assert(effects.accepted);
    assert(effects.changed);
    assert(effects.epoch_rotated);
    assert(effects.epoch_key_id == 7);
    assert(effects.keydist_confirmed);
    assert(effects.keydist_member_id == 0x22222222);
    assert(state.has_team_id);
    assert(state.team_name == "TEAM-ABCD");
    assert(state.security_round == 7);
    assert(!state.waiting_new_keys);
    assert(state.self_is_leader);
    assert(state.members.size() == 2);
    assert(state.members[0].node_id == 0);
    assert(state.members[0].name == "You");
    assert(state.members[0].leader);
    assert(state.members[1].node_id == 0x22222222);
    assert(state.members[1].name == "2222");
    assert(state.members[1].last_seen_s == 990);
}

void testStatusFromOtherTeamIsIgnored()
{
    auto reducer = makeReducer();
    team::ui::TeamPageEventState state;
    state.team_id = testTeamId();
    state.has_team_id = true;

    team::TeamStatusEvent event;
    event.ctx = makeEventContext();
    event.ctx.team_id[0] = 0x99;
    event.msg.members = {0x22222222};
    event.msg.has_members = true;

    const auto effects = reducer.reduceStatus(state, event);

    assert(!effects.accepted);
    assert(state.members.empty());
}

void testActivityTouchesSenderAndConfirmsKeys()
{
    auto reducer = makeReducer();
    team::ui::TeamPageEventState state;
    state.team_id = testTeamId();
    state.has_team_id = true;

    const auto effects =
        reducer.reduceActivity(state, makeEventContext(), 0x22222222);

    assert(effects.accepted);
    assert(effects.keydist_confirmed);
    assert(effects.keydist_key_id == 7);
    assert(state.members.size() == 1);
    assert(state.members[0].name == "2222");
    assert(state.last_update_s == 990);
}

void testMemberNamesUseInjectedResolver()
{
    PrefixMemberNameResolver names;
    auto reducer = makeReducer(names);
    team::ui::TeamPageEventState state;
    state.team_id = testTeamId();
    state.has_team_id = true;

    const auto effects =
        reducer.reduceActivity(state, makeEventContext(), 0x22222222);

    assert(effects.accepted);
    assert(state.members.size() == 1);
    assert(state.members[0].name == "N2222");
}

void testActivityCanTouchSelfPlaceholder()
{
    auto reducer = makeReducer();
    team::ui::TeamPageEventState state;
    state.team_id = testTeamId();
    state.has_team_id = true;

    auto context = makeEventContext();
    context.from = 0;
    context.key_id = 0;
    const auto effects = reducer.reduceActivity(state, context, 0);

    assert(effects.accepted);
    assert(!effects.keydist_confirmed);
    assert(state.members.size() == 1);
    assert(state.members[0].node_id == 0);
    assert(state.members[0].name == "You");
    assert(state.members[0].last_seen_s == 990);
}

void testErrorSetsWaitingNewKeysOnlyForMembers()
{
    auto reducer = makeReducer();
    team::ui::TeamPageEventState state;
    state.in_team = true;
    state.self_is_leader = false;
    state.team_id = testTeamId();
    state.has_team_id = true;

    team::TeamErrorEvent event;
    event.ctx = makeEventContext();
    event.error = team::TeamProtocolError::KeyMismatch;

    const auto first = reducer.reduceError(state, event);
    const auto second = reducer.reduceError(state, event);

    assert(first.accepted);
    assert(first.show_key_mismatch);
    assert(state.waiting_new_keys);
    assert(second.accepted);
    assert(!second.show_key_mismatch);

    state.self_is_leader = true;
    state.waiting_new_keys = false;
    const auto leader = reducer.reduceError(state, event);
    assert(!leader.accepted);
    assert(!state.waiting_new_keys);
}

void testTransferLeaderUpdatesRoster()
{
    auto reducer = makeReducer();
    team::ui::TeamPageEventState state;
    team::ui::TeamMemberUi self;
    self.node_id = 0;
    self.name = "You";
    self.leader = true;
    state.members.push_back(self);
    state.self_is_leader = true;

    team::TeamTransferLeaderEvent event;
    event.msg.target = 0x33333333;

    const auto effects = reducer.reduceTransferLeader(state, event);

    assert(effects.accepted);
    assert(!state.self_is_leader);
    assert(state.members.size() == 2);
    assert(!state.members[0].leader);
    assert(state.members[1].node_id == 0x33333333);
    assert(state.members[1].name == "3333");
    assert(state.members[1].leader);
}

void testFillStatusMembersUsesSelfNodeId()
{
    auto reducer = makeReducer();
    team::ui::TeamPageEventState state;
    state.self_is_leader = true;
    team::ui::TeamMemberUi self;
    self.node_id = 0;
    self.leader = true;
    state.members.push_back(self);
    team::ui::TeamMemberUi other;
    other.node_id = 0x22222222;
    state.members.push_back(other);

    team::proto::TeamStatus status;
    reducer.fillStatusMembers(state, status);

    assert(status.has_members);
    assert(status.members.size() == 2);
    assert(status.members[0] == 0x11111111);
    assert(status.members[1] == 0x22222222);
    assert(status.leader_id == 0x11111111);
}

void testKickRemovesMemberAndRotatesEpoch()
{
    auto reducer = makeReducer();
    team::ui::TeamPageEventState state;
    state.team_id = testTeamId();
    state.has_team_id = true;
    state.security_round = 7;
    state.members.push_back({0, "You", false, true, 100, 0});
    state.members.push_back({0x22222222, "Ada", false, false, 100, 0});

    team::TeamKickEvent event;
    event.ctx = makeEventContext();
    event.msg.target = 0x22222222;

    const auto effects = reducer.reduceKick(state, event);

    assert(effects.accepted);
    assert(effects.member_kicked_key_event);
    assert(effects.member_kicked_id == 0x22222222);
    assert(effects.epoch_rotated);
    assert(effects.epoch_key_id == 8);
    assert(state.security_round == 8);
    assert(state.members.size() == 1);
    assert(state.members[0].node_id == 0);
}

void testKickSelfResetsLocalTeamAfterEffects()
{
    auto reducer = makeReducer();
    team::ui::TeamPageEventState state;
    state.in_team = true;
    state.team_id = testTeamId();
    state.has_team_id = true;
    state.has_team_psk = true;
    state.security_round = 2;
    state.members.push_back({0, "You", false, true, 100, 0});

    team::TeamKickEvent event;
    event.ctx = makeEventContext();
    event.msg.target = 0;

    const auto effects = reducer.reduceKick(state, event);

    assert(effects.accepted);
    assert(effects.member_kicked_key_event);
    assert(effects.member_kicked_id == 0);
    assert(effects.epoch_rotated);
    assert(effects.epoch_key_id == 3);
    assert(effects.clear_keys);
    assert(effects.stop_pairing);
    assert(effects.request_kicked_out_page);
    assert(!state.in_team);
    assert(state.kicked_out);
    assert(!state.has_team_id);
    assert(!state.has_team_psk);
    assert(state.security_round == 0);
    assert(state.members.empty());
}

void testKeyDistStoresKeysAndTransitionsFromPairing()
{
    auto reducer = makeReducer();
    team::ui::TeamPageEventState state;
    state.pending_join = true;
    state.pairing_role = team::TeamPairingRole::Member;

    team::TeamKeyDistEvent event;
    event.ctx = makeEventContext();
    event.msg.team_id = testTeamId();
    event.msg.key_id = 9;
    event.msg.channel_psk_len = team::proto::kTeamChannelPskSize;
    event.msg.channel_psk[0] = 0xA5;

    const auto effects = reducer.reduceKeyDist(state, event);

    assert(effects.accepted);
    assert(effects.save_keys);
    assert(effects.apply_keys);
    assert(effects.request_status_in_team_page);
    assert(effects.clear_nav_stack);
    assert(state.in_team);
    assert(!state.pending_join);
    assert(!state.kicked_out);
    assert(!state.self_is_leader);
    assert(state.has_team_id);
    assert(state.team_name == "TEAM-ABCD");
    assert(state.security_round == 9);
    assert(state.has_team_psk);
    assert(state.team_psk[0] == 0xA5);
    assert(state.members.size() == 2);
    assert(state.members[0].node_id == 0);
    assert(state.members[1].node_id == 0x22222222);
    assert(state.members[1].leader);
}

void testPairingLeaderBeaconAcceptsNewMember()
{
    auto reducer = makeReducer();
    team::ui::TeamPageEventState state;
    state.has_team_id = true;
    state.team_id = testTeamId();
    state.security_round = 4;

    team::ui::TeamPagePairingUpdate update;
    update.role = team::TeamPairingRole::Leader;
    update.state = team::TeamPairingState::LeaderBeacon;
    update.peer_id = 0x22222222;

    const auto effects = reducer.reducePairing(state, update);

    assert(effects.accepted);
    assert(effects.status_should_send);
    assert(effects.member_accepted_key_event);
    assert(effects.member_accepted_id == 0x22222222);
    assert(effects.show_pairing_peer);
    assert(state.in_team);
    assert(state.self_is_leader);
    assert(state.members.size() == 2);
    assert(state.members[0].node_id == 0);
    assert(state.members[0].leader);
    assert(state.members[1].node_id == 0x22222222);
    assert(state.last_update_s == 1000);
}

void testPairingCompletedAndFailedDrivePageEffects()
{
    auto reducer = makeReducer();
    team::ui::TeamPageEventState state;
    state.pending_join = true;
    state.pending_join_started_s = 11;

    team::ui::TeamPagePairingUpdate completed;
    completed.role = team::TeamPairingRole::Member;
    completed.state = team::TeamPairingState::Completed;

    const auto done = reducer.reducePairing(state, completed);

    assert(done.accepted);
    assert(done.request_status_in_team_page);
    assert(done.clear_nav_stack);
    assert(done.show_pairing_success);
    assert(state.in_team);
    assert(!state.pending_join);
    assert(!state.self_is_leader);
    assert(state.members.size() == 1);

    team::ui::TeamPageEventState failed_state;
    failed_state.pending_join = true;
    team::ui::TeamPagePairingUpdate failed;
    failed.state = team::TeamPairingState::Failed;

    const auto fail = reducer.reducePairing(failed_state, failed);

    assert(fail.accepted);
    assert(fail.request_status_not_in_team_page);
    assert(fail.clear_nav_stack);
    assert(fail.show_pairing_failed);
    assert(!failed_state.pending_join);
}

void testOwnRadioIdentityTouchesTheLocalAlias()
{
    auto reducer = makeReducer();
    team::ui::TeamPageEventState state;
    state.in_team = state.has_team_id = true;
    state.team_id = testTeamId();
    team::ui::TeamMemberUi self;
    self.node_id = 0;
    self.name = "You";
    state.members.push_back(self);
    auto context = makeEventContext();
    context.from = 0x11111111;
    assert(reducer.reduceActivity(state, context, context.from).accepted);
    assert(state.members.size() == 1);
    assert(state.members[0].node_id == 0 && state.members[0].name == "You");
    assert(state.members[0].last_seen_s == context.timestamp);
}

} // namespace

int main()
{
    testOwnRadioIdentityTouchesTheLocalAlias();
    testStatusRostersPreservePresenceAndLeader();
    testStatusFromOtherTeamIsIgnored();
    testActivityTouchesSenderAndConfirmsKeys();
    testMemberNamesUseInjectedResolver();
    testActivityCanTouchSelfPlaceholder();
    testErrorSetsWaitingNewKeysOnlyForMembers();
    testTransferLeaderUpdatesRoster();
    testFillStatusMembersUsesSelfNodeId();
    testKickRemovesMemberAndRotatesEpoch();
    testKickSelfResetsLocalTeamAfterEffects();
    testKeyDistStoresKeysAndTransitionsFromPairing();
    testPairingLeaderBeaconAcceptsNewMember();
    testPairingCompletedAndFailedDrivePageEffects();
    return 0;
}

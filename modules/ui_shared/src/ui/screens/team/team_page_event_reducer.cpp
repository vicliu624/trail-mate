#include "ui/screens/team/team_page_event_reducer.h"

#include "ui/team_presence/team_presence_model.h"

#include <algorithm>
#include <cstdio>

namespace team
{
namespace ui
{

TeamPageEventReducer::TeamPageEventReducer(
    TeamPageEventContext context,
    const ITeamPageMemberNameResolver& names)
    : context_(context), names_(&names)
{
}

int TeamPageEventReducer::findMemberIndex(const TeamPageEventState& state,
                                          uint32_t node_id) const
{
    const size_t index =
        ::ui::team_presence::findTeamMemberIndex(state.members, node_id);
    if (index == ::ui::team_presence::kInvalidTeamMemberIndex)
    {
        return -1;
    }
    return static_cast<int>(index);
}

void TeamPageEventReducer::touchMember(TeamPageEventState& state,
                                       uint32_t node_id,
                                       uint32_t last_seen_s) const
{
    // Radio and loopback events use the actual node ID; UI snapshots use zero
    // for the local observer. A returned local packet must not create a fourth
    // member beside the existing "You" alias.
    if (node_id == context_.self_node_id)
    {
        node_id = 0;
    }
    const uint32_t seen_s =
        ::ui::team_presence::normalizeSeenSeconds(last_seen_s,
                                                  context_.now_s);
    const auto result =
        ::ui::team_presence::touchTeamMember(state.members,
                                             node_id,
                                             seen_s);
    if (!result.valid)
    {
        return;
    }

    auto& member = state.members[result.index];
    member.name = memberDisplayLabel(node_id);
    assignMemberColor(member);
}

void TeamPageEventReducer::fillStatusMembers(
    const TeamPageEventState& state,
    team::proto::TeamStatus& status) const
{
    status.members.clear();
    status.leader_id = 0;
    for (const auto& member : state.members)
    {
        const uint32_t id =
            (member.node_id == 0) ? context_.self_node_id : member.node_id;
        if (id == 0)
        {
            continue;
        }
        if (std::find(status.members.begin(), status.members.end(), id) ==
            status.members.end())
        {
            status.members.push_back(id);
        }
        if (member.leader)
        {
            status.leader_id = id;
        }
    }
    if (status.leader_id == 0 && state.self_is_leader &&
        context_.self_node_id != 0)
    {
        status.leader_id = context_.self_node_id;
    }
    status.has_members = !status.members.empty();
}

void TeamPageEventReducer::applyStatusRoster(
    TeamPageEventState& state,
    const team::proto::TeamStatus& status) const
{
    if (!status.has_members)
    {
        return;
    }

    auto find_existing = [&](uint32_t node_id) -> const TeamMemberUi*
    {
        for (const auto& member : state.members)
        {
            if (member.node_id == node_id)
            {
                return &member;
            }
        }
        return nullptr;
    };

    std::vector<TeamMemberUi> updated;
    updated.reserve(status.members.size() + 1);
    for (uint32_t id : status.members)
    {
        if (id == 0)
        {
            continue;
        }

        const uint32_t node_id = (id == context_.self_node_id) ? 0 : id;
        TeamMemberUi entry;
        if (const TeamMemberUi* existing = find_existing(node_id))
        {
            entry = *existing;
        }
        entry.node_id = node_id;
        entry.leader = (id == status.leader_id);
        if (entry.name.empty())
        {
            entry.name = (node_id == 0) ? "You" : memberDisplayLabel(id);
        }
        if (node_id == 0 && entry.last_seen_s == 0)
        {
            entry.last_seen_s = context_.now_s;
        }
        assignMemberColor(entry);
        updated.push_back(entry);
    }

    const bool has_self =
        std::any_of(updated.begin(),
                    updated.end(),
                    [](const TeamMemberUi& member)
                    {
                        return member.node_id == 0;
                    });
    if (!has_self && context_.self_node_id != 0)
    {
        TeamMemberUi self;
        self.node_id = 0;
        self.name = "You";
        self.leader = (status.leader_id == context_.self_node_id);
        self.last_seen_s = context_.now_s;
        assignMemberColor(self);
        updated.push_back(self);
    }

    state.members = std::move(updated);
    state.self_is_leader =
        (status.leader_id != 0 && status.leader_id == context_.self_node_id);
}

TeamPageEventEffects TeamPageEventReducer::reduceError(
    TeamPageEventState& state,
    const team::TeamErrorEvent& event) const
{
    TeamPageEventEffects effects;
    if (!state.in_team || state.self_is_leader || !acceptsTeam(state, event.ctx))
    {
        return effects;
    }

    effects.accepted = true;
    if (event.error == team::TeamProtocolError::DecryptFail ||
        event.error == team::TeamProtocolError::KeyMismatch)
    {
        effects.show_key_mismatch = !state.waiting_new_keys;
        state.waiting_new_keys = true;
        effects.changed = true;
    }
    return effects;
}

TeamPageEventEffects TeamPageEventReducer::reduceStatus(
    TeamPageEventState& state,
    const team::TeamStatusEvent& event) const
{
    TeamPageEventEffects effects;
    if (!acceptsTeam(state, event.ctx))
    {
        return effects;
    }

    effects.accepted = true;
    const uint32_t prev_round = state.security_round;
    ensureTeamIdentity(state, event.ctx);
    if (event.ctx.key_id != 0)
    {
        state.security_round = event.ctx.key_id;
    }

    applyStatusRoster(state, event.msg);
    if (event.ctx.from != 0)
    {
        touchMember(state, event.ctx.from, event.ctx.timestamp);
    }

    if (event.msg.key_id != 0)
    {
        if (event.msg.key_id > state.security_round)
        {
            state.waiting_new_keys = true;
        }
        else if (event.msg.key_id == state.security_round)
        {
            state.waiting_new_keys = false;
        }
        if (event.ctx.from != 0)
        {
            effects.keydist_confirmed = true;
            effects.keydist_member_id = event.ctx.from;
            effects.keydist_key_id = event.msg.key_id;
        }
    }

    if (event.msg.key_id != 0 && event.msg.key_id > prev_round)
    {
        effects.epoch_rotated = true;
        effects.epoch_key_id = event.msg.key_id;
    }
    state.last_update_s = event.ctx.timestamp;
    effects.changed = true;
    return effects;
}

TeamPageEventEffects TeamPageEventReducer::reduceActivity(
    TeamPageEventState& state,
    const team::TeamEventContext& event,
    uint32_t member_id) const
{
    TeamPageEventEffects effects;
    if (!acceptsTeam(state, event))
    {
        return effects;
    }

    effects.accepted = true;
    ensureTeamIdentity(state, event);
    touchMember(state, member_id, event.timestamp);
    if (event.key_id != 0 && member_id != 0)
    {
        effects.keydist_confirmed = true;
        effects.keydist_member_id = member_id;
        effects.keydist_key_id = event.key_id;
    }
    state.last_update_s = event.timestamp;
    effects.changed = true;
    return effects;
}

TeamPageEventEffects TeamPageEventReducer::reduceKick(
    TeamPageEventState& state,
    const team::TeamKickEvent& event) const
{
    TeamPageEventEffects effects;
    if (!acceptsTeam(state, event.ctx))
    {
        return effects;
    }

    effects.accepted = true;
    const uint32_t target = event.msg.target;
    const int index = findMemberIndex(state, target);
    if (index >= 0)
    {
        state.members.erase(state.members.begin() + index);
    }

    effects.member_kicked_key_event = true;
    effects.member_kicked_id = target;
    state.security_round += 1;
    if (state.security_round != 0)
    {
        effects.epoch_rotated = true;
        effects.epoch_key_id = state.security_round;
    }

    if (target == 0)
    {
        resetTeamMembership(state, true);
        effects.clear_keys = true;
        effects.stop_pairing = true;
        effects.clear_keydist_pending = true;
        effects.clear_status_pending = true;
        effects.request_kicked_out_page = true;
        effects.clear_nav_stack = true;
    }

    effects.changed = true;
    return effects;
}

TeamPageEventEffects TeamPageEventReducer::reduceTransferLeader(
    TeamPageEventState& state,
    const team::TeamTransferLeaderEvent& event) const
{
    TeamPageEventEffects effects;
    const uint32_t target = event.msg.target;
    for (auto& member : state.members)
    {
        member.leader = false;
    }

    const int index = findMemberIndex(state, target);
    if (index < 0)
    {
        TeamMemberUi info;
        info.node_id = target;
        info.name = memberDisplayLabel(target);
        info.leader = true;
        info.last_seen_s = context_.now_s;
        assignMemberColor(info);
        state.members.push_back(info);
    }
    else
    {
        state.members[static_cast<size_t>(index)].leader = true;
    }
    state.self_is_leader = (target == 0);
    effects.accepted = true;
    effects.changed = true;
    return effects;
}

TeamPageEventEffects TeamPageEventReducer::reduceKeyDist(
    TeamPageEventState& state,
    const team::TeamKeyDistEvent& event) const
{
    TeamPageEventEffects effects;
    if (!acceptsTeam(state, event.ctx))
    {
        return effects;
    }

    effects.accepted = true;
    const bool was_pairing =
        state.pending_join || isPairingActive(state.pairing_state) ||
        state.pairing_role == TeamPairingRole::Member;

    state.team_id = event.msg.team_id;
    state.has_team_id = true;
    state.team_name = formatTeamNameFromId(event.msg.team_id);
    if (event.msg.key_id != 0)
    {
        state.security_round = event.msg.key_id;
    }
    if (event.msg.channel_psk_len > 0)
    {
        state.team_psk = event.msg.channel_psk;
        state.has_team_psk = true;
        effects.save_keys = true;
        effects.apply_keys = true;
    }
    state.waiting_new_keys = false;
    state.last_update_s = event.ctx.timestamp;

    if (!state.in_team || was_pairing)
    {
        state.in_team = true;
        state.kicked_out = false;
        state.pending_join = false;
        state.pending_join_started_s = 0;
        if (state.pairing_role == TeamPairingRole::Member)
        {
            state.self_is_leader = false;
        }
        ensureSelfMember(state, state.self_is_leader);
        effects.request_status_in_team_page = true;
        effects.clear_nav_stack = true;
    }

    if (!state.self_is_leader && event.ctx.from != 0)
    {
        const uint32_t leader_id = event.ctx.from;
        const int index = findMemberIndex(state, leader_id);
        if (index < 0)
        {
            TeamMemberUi leader;
            leader.node_id = leader_id;
            leader.name = memberDisplayLabel(leader_id);
            leader.leader = true;
            leader.last_seen_s = context_.now_s;
            assignMemberColor(leader);
            state.members.push_back(leader);
        }
        else
        {
            state.members[static_cast<size_t>(index)].leader = true;
        }
    }

    effects.changed = true;
    return effects;
}

TeamPageEventEffects TeamPageEventReducer::reducePairing(
    TeamPageEventState& state,
    const TeamPagePairingUpdate& update) const
{
    TeamPageEventEffects effects;
    applyPairingIdentity(state, update);

    if (update.role == TeamPairingRole::Leader)
    {
        state.in_team = true;
        state.kicked_out = false;
        state.self_is_leader = true;
        ensureSelfMember(state, true);
    }

    if (update.role == TeamPairingRole::Leader && update.peer_id != 0 &&
        update.state == TeamPairingState::LeaderBeacon)
    {
        effects.status_should_send = (findMemberIndex(state, update.peer_id) < 0);
        touchMember(state, update.peer_id, context_.now_s);
        state.last_update_s = context_.now_s;
        effects.member_accepted_key_event = true;
        effects.member_accepted_id = update.peer_id;
        effects.show_pairing_peer = true;
        effects.pairing_peer_id = update.peer_id;
    }

    if (update.state == TeamPairingState::Completed)
    {
        state.pending_join = false;
        state.pending_join_started_s = 0;
        if (!state.in_team)
        {
            state.in_team = true;
            state.kicked_out = false;
            state.self_is_leader = (update.role == TeamPairingRole::Leader);
            ensureSelfMember(state, state.self_is_leader);
        }
        effects.request_status_in_team_page = true;
        effects.clear_nav_stack = true;
        effects.show_pairing_success = true;
    }
    else if (update.state == TeamPairingState::Failed)
    {
        state.pending_join = false;
        state.pending_join_started_s = 0;
        if (state.in_team)
        {
            effects.request_status_in_team_page = true;
        }
        else
        {
            effects.request_status_not_in_team_page = true;
        }
        effects.clear_nav_stack = true;
        effects.show_pairing_failed = true;
    }
    else if (isPairingActive(update.state))
    {
        state.pending_join = true;
        if (state.pending_join_started_s == 0)
        {
            state.pending_join_started_s = context_.now_s;
        }
    }
    else
    {
        state.pending_join = false;
        state.pending_join_started_s = 0;
    }

    effects.accepted = true;
    effects.changed = true;
    return effects;
}

TeamPageEventEffects TeamPageEventReducer::reducePairingStatus(
    TeamPageEventState& state,
    const TeamPagePairingUpdate& update) const
{
    TeamPageEventEffects effects;
    applyPairingIdentity(state, update);
    if (isPairingActive(update.state))
    {
        state.pending_join = true;
        if (state.pending_join_started_s == 0)
        {
            state.pending_join_started_s = context_.now_s;
        }
    }
    else
    {
        state.pending_join = false;
        state.pending_join_started_s = 0;
    }
    effects.accepted = true;
    effects.changed = true;
    return effects;
}

std::string TeamPageEventReducer::formatTeamNameFromId(const TeamId& id)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "TEAM-%02X%02X", id[0], id[1]);
    return std::string(buf);
}

bool TeamPageEventReducer::acceptsTeam(const TeamPageEventState& state,
                                       const TeamEventContext& event) const
{
    return !state.has_team_id || event.team_id == state.team_id;
}

void TeamPageEventReducer::ensureTeamIdentity(
    TeamPageEventState& state,
    const TeamEventContext& event) const
{
    if (state.has_team_id)
    {
        return;
    }
    state.team_id = event.team_id;
    state.has_team_id = true;
    state.team_name = formatTeamNameFromId(event.team_id);
}

void TeamPageEventReducer::ensureSelfMember(TeamPageEventState& state,
                                            bool leader) const
{
    const int index = findMemberIndex(state, 0);
    if (index >= 0)
    {
        auto& self = state.members[static_cast<size_t>(index)];
        self.name = "You";
        self.leader = leader;
        if (self.last_seen_s == 0)
        {
            self.last_seen_s = context_.now_s;
        }
        assignMemberColor(self);
        return;
    }

    TeamMemberUi self;
    self.node_id = 0;
    self.name = "You";
    self.leader = leader;
    self.last_seen_s = context_.now_s;
    assignMemberColor(self);
    state.members.push_back(self);
}

void TeamPageEventReducer::resetTeamMembership(
    TeamPageEventState& state,
    bool kicked_out) const
{
    state.in_team = false;
    state.pending_join = false;
    state.pending_join_started_s = 0;
    state.kicked_out = kicked_out;
    state.self_is_leader = false;
    state.last_event_seq = 0;
    state.pairing_role = TeamPairingRole::None;
    state.pairing_state = TeamPairingState::Idle;
    state.pairing_peer_id = 0;
    state.pairing_team_name.clear();
    state.has_team_id = false;
    state.team_id = TeamId{};
    state.team_name.clear();
    state.security_round = 0;
    state.last_update_s = 0;
    state.team_psk = {};
    state.has_team_psk = false;
    state.waiting_new_keys = false;
    state.members.clear();
}

void TeamPageEventReducer::applyPairingIdentity(
    TeamPageEventState& state,
    const TeamPagePairingUpdate& update) const
{
    state.pairing_role = update.role;
    state.pairing_state = update.state;
    state.pairing_peer_id = update.peer_id;
    state.pairing_team_name.clear();
    if (update.has_team_name)
    {
        state.pairing_team_name = update.team_name;
    }
    if (update.has_team_id)
    {
        state.team_id = update.team_id;
        state.has_team_id = true;
        state.team_name = formatTeamNameFromId(update.team_id);
    }
}

bool TeamPageEventReducer::isPairingActive(TeamPairingState state) const
{
    return state != TeamPairingState::Idle &&
           state != TeamPairingState::Completed &&
           state != TeamPairingState::Failed;
}

std::string TeamPageEventReducer::memberDisplayLabel(uint32_t node_id) const
{
    if (node_id == 0)
    {
        return "You";
    }
    return names_->resolveMemberName(node_id);
}

void TeamPageEventReducer::assignMemberColor(TeamMemberUi& member) const
{
    uint32_t node_id = member.node_id;
    if (node_id == 0)
    {
        node_id = context_.self_node_id;
    }
    member.color_index = team_color_index_from_node_id(node_id);
}

} // namespace ui
} // namespace team

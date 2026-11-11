#include "ui/screens/team/team_page_state_store.h"

namespace team
{
namespace ui
{

void assignTeamPageMemberColors(std::vector<TeamMemberUi>& members,
                                TeamPageColorContext color_context)
{
    for (auto& member : members)
    {
        uint32_t node_id = member.node_id;
        if (node_id == 0)
        {
            node_id = color_context.self_node_id;
        }
        member.color_index = team_color_index_from_node_id(node_id);
    }
}

bool TeamPageStateStore::loadOnce(ITeamUiSnapshotStore& store,
                                  TeamPagePersistentState& state,
                                  TeamPageColorContext color_context)
{
    if (loaded_)
    {
        return false;
    }

    // Storage can become ready after the first UI tick during deferred boot.
    // A failed read must preserve state and leave restoration retryable.
    loaded_ = refresh(store, state, color_context);
    return loaded_;
}

bool TeamPageStateStore::refresh(ITeamUiSnapshotStore& store,
                                 TeamPagePersistentState& state,
                                 TeamPageColorContext color_context)
{
    TeamUiSnapshot snapshot;
    if (!store.load(snapshot))
    {
        return false;
    }

    applySnapshot(state, snapshot, color_context);
    return true;
}

void TeamPageStateStore::save(ITeamUiSnapshotStore& store,
                              const TeamPagePersistentState& state)
{
    // An early, ignored Team event can reach this path before storage is ready.
    // Only an explicit join or a complete new Team may initialize unread state;
    // empty/partial projections must not overwrite persisted membership/keys.
    const bool complete_team =
        state.in_team && state.has_team_id && state.has_team_psk;
    if (!loaded_ && !state.pending_join && !complete_team)
    {
        return;
    }
    store.save(snapshotFromState(state));
    // A newly created Team can be left or a join canceled before the next tick.
    loaded_ = true;
}

void TeamPageStateStore::resetLoaded()
{
    loaded_ = false;
}

TeamUiSnapshot TeamPageStateStore::snapshotFromState(
    const TeamPagePersistentState& state)
{
    TeamUiSnapshot snapshot;
    snapshot.in_team = state.in_team;
    snapshot.pending_join = state.pending_join;
    snapshot.pending_join_started_s = state.pending_join_started_s;
    snapshot.kicked_out = state.kicked_out;
    snapshot.self_is_leader = state.self_is_leader;
    snapshot.last_event_seq = state.last_event_seq;
    snapshot.team_chat_unread = state.team_chat_unread;
    snapshot.team_id = state.team_id;
    snapshot.has_team_id = state.has_team_id;
    snapshot.team_name = state.team_name;
    snapshot.security_round = state.security_round;
    snapshot.last_update_s = state.last_update_s;
    snapshot.team_psk = state.team_psk;
    snapshot.has_team_psk = state.has_team_psk;
    snapshot.members = state.members;
    return snapshot;
}

void TeamPageStateStore::applySnapshot(TeamPagePersistentState& state,
                                       const TeamUiSnapshot& snapshot,
                                       TeamPageColorContext color_context)
{
    state.in_team = snapshot.in_team;
    state.pending_join = snapshot.pending_join;
    state.pending_join_started_s = snapshot.pending_join_started_s;
    state.kicked_out = snapshot.kicked_out;
    state.self_is_leader = snapshot.self_is_leader;
    state.last_event_seq = snapshot.last_event_seq;
    state.team_chat_unread = snapshot.team_chat_unread;
    state.team_id = snapshot.team_id;
    state.has_team_id = snapshot.has_team_id;
    state.team_name = snapshot.team_name;
    state.security_round = snapshot.security_round;
    state.last_update_s = snapshot.last_update_s;
    state.team_psk = snapshot.team_psk;
    state.has_team_psk = snapshot.has_team_psk;
    state.members = snapshot.members;
    assignTeamPageMemberColors(state.members, color_context);
}

} // namespace ui
} // namespace team

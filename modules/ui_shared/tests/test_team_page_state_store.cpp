#include "ui/screens/team/team_page_state_store.h"

#include <cassert>

namespace
{

team::TeamId testTeamId()
{
    team::TeamId id{};
    id[0] = 0x12;
    id[1] = 0x34;
    return id;
}

team::ui::TeamMemberUi makeMember(uint32_t node_id, const char* name)
{
    team::ui::TeamMemberUi member;
    member.node_id = node_id;
    member.name = name;
    member.last_seen_s = 77;
    return member;
}

team::ui::TeamPagePersistentState makeState()
{
    team::ui::TeamPagePersistentState state;
    state.in_team = true;
    state.pending_join = true;
    state.pending_join_started_s = 11;
    state.kicked_out = true;
    state.self_is_leader = true;
    state.last_event_seq = 22;
    state.team_chat_unread = 33;
    state.team_id = testTeamId();
    state.has_team_id = true;
    state.team_name = "Trail";
    state.security_round = 44;
    state.last_update_s = 55;
    state.team_psk[0] = 0xAA;
    state.has_team_psk = true;
    state.members.push_back(makeMember(0, "Self"));
    state.members.push_back(makeMember(0x87654321, "Ada"));
    return state;
}

class FakeSnapshotStore final : public team::ui::ITeamUiSnapshotStore
{
  public:
    bool load(team::ui::TeamUiSnapshot& out) override
    {
        if (!has_snapshot)
        {
            return false;
        }
        out = snapshot;
        ++load_count;
        return true;
    }

    void save(const team::ui::TeamUiSnapshot& in) override
    {
        snapshot = in;
        has_snapshot = true;
        ++save_count;
    }

    void clear() override
    {
        has_snapshot = false;
    }

    bool has_snapshot = false;
    int load_count = 0;
    int save_count = 0;
    team::ui::TeamUiSnapshot snapshot;
};

void testSnapshotFromStatePreservesPersistentFields()
{
    const auto state = makeState();

    const auto snapshot =
        team::ui::TeamPageStateStore::snapshotFromState(state);

    assert(snapshot.in_team);
    assert(snapshot.pending_join);
    assert(snapshot.pending_join_started_s == 11);
    assert(snapshot.kicked_out);
    assert(snapshot.self_is_leader);
    assert(snapshot.last_event_seq == 22);
    assert(snapshot.team_chat_unread == 33);
    assert(snapshot.team_id == testTeamId());
    assert(snapshot.has_team_id);
    assert(snapshot.team_name == "Trail");
    assert(snapshot.security_round == 44);
    assert(snapshot.last_update_s == 55);
    assert(snapshot.team_psk[0] == 0xAA);
    assert(snapshot.has_team_psk);
    assert(snapshot.members.size() == 2);
}

void testApplySnapshotAssignsMemberColors()
{
    team::ui::TeamUiSnapshot snapshot =
        team::ui::TeamPageStateStore::snapshotFromState(makeState());
    snapshot.members[0].color_index = team::ui::kTeamColorUnassigned;
    snapshot.members[1].color_index = team::ui::kTeamColorUnassigned;

    team::ui::TeamPagePersistentState state;
    team::ui::TeamPageStateStore::applySnapshot(
        state,
        snapshot,
        team::ui::TeamPageColorContext(0x12345678));

    assert(state.team_name == "Trail");
    assert(state.members.size() == 2);
    assert(state.members[0].node_id == 0);
    assert(state.members[0].color_index ==
           team::ui::team_color_index_from_node_id(0x12345678));
    assert(state.members[1].color_index ==
           team::ui::team_color_index_from_node_id(0x87654321));
}

void testLoadOnceAndRefresh()
{
    FakeSnapshotStore fake;
    auto state = makeState();
    state.team_name = "Keep until storage is ready";
    team::ui::TeamPageStateStore store;

    assert(!store.loadOnce(fake, state, team::ui::TeamPageColorContext(0x12345678)));
    assert(state.team_name == "Keep until storage is ready");
    assert(state.has_team_psk && state.team_psk[0] == 0xAA);
    assert(state.in_team && state.security_round == 44);
    fake.save(team::ui::TeamPageStateStore::snapshotFromState(makeState()));

    assert(store.loadOnce(fake,
                          state,
                          team::ui::TeamPageColorContext(0x12345678)));
    assert(state.team_name == "Trail");
    assert(fake.load_count == 1);

    fake.snapshot.team_name = "Changed";
    assert(!store.loadOnce(fake,
                           state,
                           team::ui::TeamPageColorContext(0x12345678)));
    assert(state.team_name == "Trail");
    assert(fake.load_count == 1);

    assert(store.refresh(fake,
                         state,
                         team::ui::TeamPageColorContext(0x12345678)));
    assert(state.team_name == "Changed");
    assert(fake.load_count == 2);

    store.resetLoaded();
    fake.snapshot.team_name = "Reloaded";
    assert(store.loadOnce(fake,
                          state,
                          team::ui::TeamPageColorContext(0x12345678)));
    assert(state.team_name == "Reloaded");
}

void testSaveWritesSnapshot()
{
    FakeSnapshotStore fake;
    fake.snapshot = team::ui::TeamPageStateStore::snapshotFromState(makeState());
    team::ui::TeamPageStateStore store;

    // A transiently unavailable old Team must survive an automatic empty save.
    store.save(fake, {});
    assert(fake.save_count == 0);
    team::ui::TeamPagePersistentState partial;
    partial.has_team_id = true;
    partial.team_id[0] = 0x99;
    store.save(fake, partial);
    assert(fake.save_count == 0);
    assert(fake.snapshot.has_team_psk && fake.snapshot.team_psk[0] == 0xAA);

    auto state = makeState();
    state.pending_join = false;
    state.kicked_out = false;
    store.save(fake, state);

    assert(fake.save_count == 1);
    assert(fake.has_snapshot);
    assert(fake.snapshot.team_name == "Trail");
    assert(fake.snapshot.members.size() == 2);

    // Explicit creation establishes ownership, so immediate leave still clears.
    store.save(fake, {});
    assert(fake.save_count == 2);
    assert(!fake.snapshot.in_team && !fake.snapshot.has_team_psk);

    FakeSnapshotStore joining;
    team::ui::TeamPageStateStore join_store;
    team::ui::TeamPagePersistentState pending;
    pending.pending_join = true;
    join_store.save(joining, pending);
    assert(joining.save_count == 1 && joining.snapshot.pending_join);
    join_store.save(joining, {});
    assert(joining.save_count == 2 && !joining.snapshot.pending_join);
}

} // namespace

int main()
{
    testSnapshotFromStatePreservesPersistentFields();
    testApplySnapshotAssignsMemberColors();
    testLoadOnceAndRefresh();
    testSaveWritesSnapshot();
    return 0;
}

#include "ui/screens/team/team_page_deferred_dispatch.h"

#include <cassert>

namespace
{

class FakeDispatchPort final
    : public team::ui::ITeamPageDeferredDispatchPort
{
  public:
    bool hasController() const override { return has_controller; }

    bool sendKeyDistPlain(const team::proto::TeamKeyDist& key_dist,
                          chat::ChannelId,
                          chat::NodeId dest) override
    {
        keydist_send_count += 1;
        last_keydist = key_dist;
        last_keydist_dest = dest;
        return keydist_ok;
    }

    bool sendStatus(const team::proto::TeamStatus& status,
                    chat::ChannelId,
                    chat::NodeId dest) override
    {
        status_send_count += 1;
        last_status = status;
        last_status_dest = dest;
        return status_ok;
    }

    bool sendStatusPlain(const team::proto::TeamStatus& status,
                         chat::ChannelId,
                         chat::NodeId dest) override
    {
        status_plain_send_count += 1;
        last_status = status;
        last_status_plain_dest = dest;
        return status_plain_ok;
    }

    team::TeamService::SendError lastSendError() const override
    {
        return error;
    }

    bool has_controller = true;
    bool keydist_ok = true;
    bool status_ok = true;
    bool status_plain_ok = true;
    team::TeamService::SendError error =
        team::TeamService::SendError::None;
    int keydist_send_count = 0;
    int status_send_count = 0;
    int status_plain_send_count = 0;
    chat::NodeId last_keydist_dest = 0;
    chat::NodeId last_status_dest = 1;
    chat::NodeId last_status_plain_dest = 1;
    team::proto::TeamKeyDist last_keydist;
    team::proto::TeamStatus last_status;
};

team::TeamId testTeamId()
{
    team::TeamId id{};
    id[0] = 0xCA;
    id[1] = 0xFE;
    return id;
}

team::ui::TeamPageDeferredDispatchState readyState()
{
    team::ui::TeamPageDeferredDispatchState state;
    state.in_team = true;
    state.has_team_id = true;
    state.self_is_leader = true;
    state.team_id = testTeamId();
    state.security_round = 12;
    state.has_team_psk = true;
    state.team_psk[0] = 0x42;
    return state;
}

team::ui::TeamPageDeferredDispatchQueue makeQueue()
{
    team::ui::TeamPageDeferredDispatchConfig config;
    config.keydist_max_retries = 2;
    config.keydist_retry_interval_s = 5;
    config.status_rebroadcast_interval_s = 2;
    return team::ui::TeamPageDeferredDispatchQueue(config);
}

void testKeyDistRetriesRespectDelayAndDeduplicate()
{
    auto queue = makeQueue();
    FakeDispatchPort port;
    auto state = readyState();

    queue.enqueueKeyDist(0x22222222, 13, 100);
    queue.enqueueKeyDist(0x22222222, 13, 100);
    assert(queue.keyDistPendingCount() == 1);

    auto effects = queue.processKeyDistRetries(state, port, 104);
    assert(!effects.sent_keydist);
    assert(port.keydist_send_count == 0);

    effects = queue.processKeyDistRetries(state, port, 105);
    assert(effects.sent_keydist);
    assert(port.keydist_send_count == 1);
    assert(port.last_keydist_dest == 0x22222222);
    assert(port.last_keydist.team_id == testTeamId());
    assert(port.last_keydist.key_id == 13);
    assert(port.last_keydist.channel_psk_len ==
           team::proto::kTeamChannelPskSize);
    assert(port.last_keydist.channel_psk[0] == 0x42);

    queue.confirmKeyDist(0x22222222, 13);
    assert(queue.keyDistPendingCount() == 0);
}

void testKeyDistFailureEffects()
{
    auto queue = makeQueue();
    FakeDispatchPort port;
    auto state = readyState();
    port.keydist_ok = false;
    port.error = team::TeamService::SendError::MeshSendFail;

    queue.enqueueKeyDist(0x33333333, 21, 0);
    auto effects = queue.processKeyDistRetries(state, port, 5);

    assert(effects.sent_keydist);
    assert(effects.failures.size() == 1);
    assert(effects.failures[0].action ==
           team::ui::TeamPageDeferredDispatchAction::KeyDist);
    assert(effects.failures[0].kind ==
           team::ui::TeamPageDeferredDispatchFailureKind::SendFailedDetail);
    assert(effects.failures[0].error ==
           team::TeamService::SendError::MeshSendFail);

    port.keydist_ok = true;
    effects = queue.processKeyDistRetries(state, port, 10);
    assert(effects.failures.empty());
    effects = queue.processKeyDistRetries(state, port, 15);
    assert(effects.failures.size() == 1);
    assert(effects.failures[0].kind ==
           team::ui::TeamPageDeferredDispatchFailureKind::SendFailed);
    assert(queue.keyDistPendingCount() == 0);
}

void testKeyDistMissingRuntimeDoesNothing()
{
    auto queue = makeQueue();
    FakeDispatchPort port;
    auto state = readyState();
    port.has_controller = false;

    queue.enqueueKeyDist(0x33333333, 21, 0);
    auto effects = queue.processKeyDistRetries(state, port, 5);

    assert(!effects.sent_keydist);
    assert(port.keydist_send_count == 0);
    assert(queue.keyDistPendingCount() == 1);

    state.has_team_psk = false;
    port.has_controller = true;
    effects = queue.processKeyDistRetries(state, port, 10);
    assert(!effects.sent_keydist);
    assert(port.keydist_send_count == 0);
}

void testStatusBroadcastSendsAndReschedules()
{
    auto queue = makeQueue();
    FakeDispatchPort port;
    auto state = readyState();
    team::proto::TeamStatus status;
    status.key_id = 12;
    status.members = {0x11111111, 0x22222222};
    status.has_members = true;
    status.leader_id = 0x11111111;

    queue.scheduleStatusBroadcast(2, 100, 3);
    assert(queue.statusBroadcastPendingCount() == 1);

    auto effects = queue.processStatusBroadcasts(state, status, port, 102);
    assert(!effects.sent_status);
    assert(port.status_send_count == 0);

    effects = queue.processStatusBroadcasts(state, status, port, 103);
    assert(effects.sent_status);
    assert(port.status_send_count == 1);
    assert(port.status_plain_send_count == 1);
    assert(port.last_status.key_id == 12);
    assert(queue.statusBroadcastPendingCount() == 1);

    effects = queue.processStatusBroadcasts(state, status, port, 105);
    assert(effects.sent_status);
    assert(port.status_send_count == 2);
    assert(port.status_plain_send_count == 2);
    assert(queue.statusBroadcastPendingCount() == 0);
}

void testStatusBroadcastFailureAndInvalidLeaderState()
{
    auto queue = makeQueue();
    FakeDispatchPort port;
    auto state = readyState();
    team::proto::TeamStatus status;
    status.key_id = 3;
    port.status_ok = false;
    port.status_plain_ok = false;

    queue.scheduleStatusBroadcast(1, 0, 0);
    auto effects = queue.processStatusBroadcasts(state, status, port, 0);

    assert(effects.sent_status);
    assert(effects.failures.size() == 2);
    assert(effects.failures[0].action ==
           team::ui::TeamPageDeferredDispatchAction::Status);
    assert(effects.failures[0].needs_keys);
    assert(!effects.failures[1].needs_keys);

    queue.scheduleStatusBroadcast(1, 10, 0);
    state.self_is_leader = false;
    effects = queue.processStatusBroadcasts(state, status, port, 10);
    assert(!effects.sent_status);
    assert(queue.statusBroadcastPendingCount() == 0);
}

void testPeriodicRosterRepairsMissedEnrollmentBroadcast()
{
    auto queue = makeQueue();
    FakeDispatchPort port;
    auto state = readyState();
    team::proto::TeamStatus status;
    status.key_id = state.security_round;
    status.has_members = true;
    status.leader_id = 0x11111111;
    status.members = {0x11111111, 0x22222222, 0x33333333};

    // No pairing event or queued retry: a restored leader still advertises its
    // complete roster. A late receiver can miss every enrollment broadcast.
    queue.processStatusBroadcasts(state, status, port, 100);
    queue.processStatusBroadcasts(state, status, port, 101);
    assert(port.status_send_count == 0);
    auto effects = queue.processStatusBroadcasts(state, status, port, 105);
    assert(effects.sent_status && port.status_send_count == 1);
    assert(port.last_status.has_members && port.last_status.members == status.members);
    assert(port.status_plain_send_count == 0);
    assert(queue.statusBroadcastPendingCount() == 0);
    for (uint32_t now = 106; now < 135; ++now)
        queue.processStatusBroadcasts(state, status, port, now);
    assert(port.status_send_count == 1);
    queue.processStatusBroadcasts(state, status, port, 135);
    assert(port.status_send_count == 2);

    // A failed carrier retries on a bounded delay, rather than every UI tick.
    port.status_ok = false;
    effects = queue.processStatusBroadcasts(state, status, port, 165);
    assert(!effects.sent_status && effects.failures.empty());
    assert(port.status_send_count == 3);
    for (uint32_t now = 166; now < 170; ++now)
        queue.processStatusBroadcasts(state, status, port, now);
    assert(port.status_send_count == 3);
    port.status_ok = true;
    queue.processStatusBroadcasts(state, status, port, 170);
    assert(port.status_send_count == 4);

    state.in_team = false;
    queue.processStatusBroadcasts(state, status, port, 180);
    queue.processStatusBroadcasts(state, status, port, 1000);
    assert(port.status_send_count == 4);
}

void testPeriodicMemberPresenceDoesNotReplaceLeaderRoster()
{
    auto queue = makeQueue();
    FakeDispatchPort port;
    auto state = readyState();
    state.self_is_leader = false;
    team::proto::TeamStatus status;
    status.key_id = state.security_round;
    queue.processStatusBroadcasts(state, status, port, 100);
    queue.processStatusBroadcasts(state, status, port, 105);
    assert(port.status_send_count == 1);
    assert(!port.last_status.has_members && port.last_status.members.empty());
    assert(port.status_plain_send_count == 0);

    // Membership credentials never become periodic plaintext status.
    state.has_team_psk = false;
    queue.processStatusBroadcasts(state, status, port, 135);
    assert(port.status_send_count == 1 && port.status_plain_send_count == 0);

    state.has_team_psk = true;
    queue.processStatusBroadcasts(state, status, port, UINT32_MAX - 1);
    queue.processStatusBroadcasts(state, status, port, 4);
    assert(port.status_send_count == 2);
}

} // namespace

int main()
{
    testKeyDistRetriesRespectDelayAndDeduplicate();
    testKeyDistFailureEffects();
    testKeyDistMissingRuntimeDoesNothing();
    testStatusBroadcastSendsAndReschedules();
    testStatusBroadcastFailureAndInvalidLeaderState();
    testPeriodicRosterRepairsMissedEnrollmentBroadcast();
    testPeriodicMemberPresenceDoesNotReplaceLeaderRoster();
    return 0;
}

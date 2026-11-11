#include "app/app_system_tick.h"
#include "platform/linux/team_protocol_crypto.h"
#include "platform/ui/team_ui_snapshot_store.h"
#include "ui/screens/team/team_page_deferred_dispatch.h"
#include "ui/screens/team/team_page_event_reducer.h"
#include <algorithm>
#include <cassert>
#include <cstring>

class Runtime final : public team::ITeamRuntime
{
  public:
    uint32_t now = 0;
    uint8_t entropy = 1;
    uint32_t nowMillis() override { return now; }
    uint32_t nowUnixSeconds() override { return 1000 + now / 1000; }
    void fillRandomBytes(uint8_t* output, size_t count) override
    {
        while (count--) *output++ = entropy++;
    }
};

class Mesh final : public chat::IMeshAdapter
{
  public:
    uint32_t self = 0x1111;
    bool send_ok = true;
    unsigned attempts = 0;
    std::vector<chat::MeshIncomingData> sent, incoming;
    chat::MeshCapabilities getCapabilities() const override
    {
        chat::MeshCapabilities caps;
        caps.supports_broadcast_appdata = caps.provides_appdata_sender = true;
        return caps;
    }
    bool sendText(chat::ChannelId, const std::string&, chat::MessageId*, chat::NodeId = 0) override { return false; }
    bool pollIncomingText(chat::MeshIncomingText*) override { return false; }
    bool sendAppData(chat::ChannelId channel, uint32_t port, const uint8_t* data, size_t count,
                     chat::NodeId peer = 0, bool = false, chat::MessageId = 0, bool = false) override
    {
        ++attempts;
        if (!send_ok) return false;
        chat::MeshIncomingData frame;
        frame.from = self;
        frame.to = peer;
        frame.channel = channel;
        frame.portnum = port;
        frame.payload.assign(data, data + count);
        sent.push_back(std::move(frame));
        return true;
    }
    bool pollIncomingData(chat::MeshIncomingData* frame) override
    {
        if (incoming.empty()) return false;
        *frame = incoming.back();
        incoming.pop_back();
        return true;
    }
    void applyConfig(const chat::MeshConfig&) override {}
    bool isReady() const override { return true; }
    bool pollIncomingRawPacket(uint8_t*, size_t&, size_t) override { return false; }
    chat::NodeId getNodeId() const override { return self; }
};

class Names final : public team::ui::ITeamPageMemberNameResolver
{
  public:
    std::string resolveMemberName(uint32_t) const override { return "Peer"; }
};

class Sink final : public team::ITeamEventSink
{
  public:
    unsigned statuses = 0, errors = 0;
    team::TeamStatusEvent last;
    void onTeamStatus(const team::TeamStatusEvent& event) override
    {
        last = event;
        ++statuses;
    }
    void onTeamKick(const team::TeamKickEvent&) override {}
    void onTeamTransferLeader(const team::TeamTransferLeaderEvent&) override {}
    void onTeamKeyDist(const team::TeamKeyDistEvent&) override {}
    void onTeamKeyRequest(const team::TeamKeyRequestEvent&) override {}
    void onTeamPosition(const team::TeamPositionEvent&) override {}
    void onTeamWaypoint(const team::TeamWaypointEvent&) override {}
    void onTeamTrack(const team::TeamTrackEvent&) override {}
    void onTeamChat(const team::TeamChatEvent&) override {}
    void onTeamError(const team::TeamErrorEvent&) override { ++errors; }
};

class Dispatch final : public team::ui::ITeamPageDeferredDispatchPort
{
  public:
    explicit Dispatch(team::TeamService& service) : service_(service) {}
    bool hasController() const override { return true; }
    bool sendKeyDistPlain(const team::proto::TeamKeyDist&, chat::ChannelId, chat::NodeId) override { return false; }
    bool sendStatus(const team::proto::TeamStatus& status, chat::ChannelId channel, chat::NodeId peer) override
    {
        return service_.sendStatus(status, channel, peer);
    }
    bool sendStatusPlain(const team::proto::TeamStatus& status, chat::ChannelId channel, chat::NodeId peer) override
    {
        return service_.sendStatusPlain(status, channel, peer);
    }
    team::TeamService::SendError lastSendError() const override { return service_.getLastSendError(); }

  private:
    team::TeamService& service_;
};

void testSystemTickCadence()
{
    assert(sys::EventBus::init(1));
    app::AppSystemTickPublisher publisher;
    assert(publisher.publish(0));
    assert(!publisher.publish(999));
    assert(sys::EventBus::pendingCount() == 1);
    // A full bus drops the tick without spinning or retaining another event.
    assert(!publisher.publish(1000));
    sys::Event* event = nullptr;
    assert(sys::EventBus::subscribe(&event, 0));
    assert(event && event->type == sys::EventType::SystemTick);
    delete event;
    assert(!publisher.publish(1001));
    assert(publisher.publish(2000));
    assert(sys::EventBus::subscribe(&event, 0));
    delete event;
    assert(!sys::EventBus::subscribe(&event, 0));

    app::AppSystemTickPublisher rollover;
    assert(rollover.publish(UINT32_MAX - 499U));
    assert(sys::EventBus::subscribe(&event, 0));
    delete event;
    assert(!rollover.publish(499));
    assert(rollover.publish(500));
    assert(sys::EventBus::subscribe(&event, 0));
    delete event;
    assert(sizeof(app::AppSystemTickPublisher) <= 8);
    assert(sys::EventBus::init());
}

int main()
{
    testSystemTickCadence();
    Runtime runtime;
    Mesh alice, bob;
    bob.self = 0x2222;
    Sink alice_sink, bob_sink;
    platform::linux_runtime::TeamProtocolCrypto crypto;
    team::TeamService a(crypto, alice, alice_sink, runtime), b(crypto, bob, bob_sink, runtime);
    team::ui::TeamUiSnapshot snapshot;
    snapshot.in_team = snapshot.has_team_id = snapshot.has_team_psk = true;
    snapshot.team_id = {1, 2, 3, 4, 5, 6, 7, 8};
    snapshot.security_round = 7;
    snapshot.team_psk.fill(42);
    assert(a.setKeysFromPsk(snapshot.team_id, 7, snapshot.team_psk.data(), 16));
    assert(b.setKeysFromPsk(snapshot.team_id, 7, snapshot.team_psk.data(), 16));
    team::ui::TeamPageDeferredDispatchQueue presence;
    team::ui::TeamPageDeferredDispatchState dispatch_state;
    dispatch_state.in_team = dispatch_state.has_team_id = dispatch_state.has_team_psk = true;
    dispatch_state.team_id = snapshot.team_id;
    dispatch_state.security_round = 7;
    Dispatch port(a);
    team::proto::TeamStatus status;
    status.key_id = 7;
    app::AppSystemTickPublisher system_tick;
    const auto pump = [&](uint32_t seconds)
    {
        runtime.now = (seconds - 1000U) * 1000U;
        system_tick.publish(runtime.now);
        sys::Event* event = nullptr;
        while (sys::EventBus::subscribe(&event, 0))
        {
            assert(event && event->type == sys::EventType::SystemTick);
            presence.processStatusBroadcasts(dispatch_state, status, port, seconds);
            delete event;
        }
    };
    pump(1000);
    assert(alice.sent.empty());
    pump(1005);
    assert(alice.sent.size() == 1);

    // A late member understands the ordinary authenticated Status wire without
    // a synthetic roster. Existing ESP and Linux reducers retain the leader.
    bob.incoming.push_back(alice.sent.back());
    b.processIncoming();
    assert(bob_sink.statuses == 1 && bob_sink.errors == 0);
    assert(!bob_sink.last.msg.has_members && bob_sink.last.ctx.from == alice.self);
    Names names;
    team::ui::TeamPageEventReducer reducer({.now_s = 1010, .self_node_id = bob.self}, names);
    team::ui::TeamPageEventState state;
    state.in_team = state.has_team_id = true;
    state.team_id = snapshot.team_id;
    state.security_round = 7;
    team::ui::TeamMemberUi self, leader;
    self.node_id = 0;
    leader.node_id = 0x9A94;
    leader.leader = true;
    state.members = {self, leader};
    assert(reducer.reduceStatus(state, bob_sink.last).accepted);
    assert(state.members.size() == 3 && state.members[1].leader);

    for (unsigned i = 0; i < 10000; ++i)
        presence.processStatusBroadcasts(dispatch_state, status, port, 1006);
    assert(alice.sent.size() == 1);
    alice.send_ok = false;
    presence.processStatusBroadcasts(dispatch_state, status, port, 1035);
    const auto failed_attempts = alice.attempts;
    for (unsigned i = 0; i < 1000; ++i)
        presence.processStatusBroadcasts(dispatch_state, status, port, 1036);
    assert(alice.attempts == failed_attempts);
    alice.send_ok = true;
    presence.processStatusBroadcasts(dispatch_state, status, port, 1040);
    assert(alice.sent.size() == 2);

    // A receiver that missed all enrollment broadcasts learns the whole roster
    // through the actual shared scheduler, TeamService and authenticated codec.
    dispatch_state.self_is_leader = true;
    status.has_members = true;
    status.leader_id = alice.self;
    status.members = {alice.self, bob.self, 0x9A94};
    pump(1070);
    bob.incoming.push_back(alice.sent.back());
    b.processIncoming();
    assert(bob_sink.last.msg.has_members && bob_sink.last.msg.leader_id == alice.self);
    assert(std::find(bob_sink.last.msg.members.begin(), bob_sink.last.msg.members.end(), 0) == bob_sink.last.msg.members.end());
    assert(reducer.reduceStatus(state, bob_sink.last).accepted);
    assert(state.members.size() == 3 && !state.self_is_leader);
    assert(reducer.findMemberIndex(state, 0) >= 0);
    assert(reducer.findMemberIndex(state, alice.self) >= 0);
    assert(reducer.findMemberIndex(state, 0x9A94) >= 0);

    const auto sent = alice.sent.size();
    dispatch_state.in_team = false;
    presence.processStatusBroadcasts(dispatch_state, status, port, 1080);
    presence.processStatusBroadcasts(dispatch_state, status, port, 1200);
    assert(alice.sent.size() == sent);
    dispatch_state.in_team = true;
    presence.processStatusBroadcasts(dispatch_state, status, port, UINT32_MAX - 1);
    presence.processStatusBroadcasts(dispatch_state, status, port, 4);
    assert(alice.sent.size() == sent + 1);
    a.clearKeys();
    presence.processStatusBroadcasts(dispatch_state, status, port, 34);
    assert(alice.sent.size() == sent + 1);
}

#include "platform/linux/team_presence.h"
#include "platform/linux/team_protocol_crypto.h"
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

int main()
{
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
    platform::linux_runtime::LinuxTeamPresence presence(a, runtime);
    presence.update(snapshot, alice.self, true);
    assert(alice.sent.empty());
    runtime.now = 10000;
    presence.update(snapshot, alice.self, true);
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

    for (unsigned i = 0; i < 10000; ++i) presence.update(snapshot, alice.self, true);
    assert(alice.sent.size() == 1);
    presence.update(snapshot, alice.self, false);
    runtime.now += 120000;
    presence.update(snapshot, alice.self, false);
    assert(alice.sent.size() == 1);
    presence.update(snapshot, alice.self, true);
    alice.send_ok = false;
    runtime.now += 10000;
    presence.update(snapshot, alice.self, true);
    const auto failed_attempts = alice.attempts;
    for (unsigned i = 0; i < 1000; ++i) presence.update(snapshot, alice.self, true);
    assert(alice.attempts == failed_attempts);
    alice.send_ok = true;
    runtime.now += 10000;
    presence.update(snapshot, alice.self, true);
    assert(alice.sent.size() == 2);

    // Only a leader supplies a complete roster, with the local UI alias resolved.
    snapshot.self_is_leader = true;
    snapshot.members = {self, leader};
    presence.update(snapshot, alice.self, true);
    runtime.now += 10000;
    presence.update(snapshot, alice.self, true);
    bob.incoming.push_back(alice.sent.back());
    b.processIncoming();
    assert(bob_sink.last.msg.has_members && bob_sink.last.msg.leader_id == alice.self);
    assert(std::find(bob_sink.last.msg.members.begin(), bob_sink.last.msg.members.end(), 0) == bob_sink.last.msg.members.end());

    const auto sent = alice.sent.size();
    snapshot.in_team = false;
    presence.update(snapshot, alice.self, true);
    runtime.now += 120000;
    presence.update(snapshot, alice.self, true);
    assert(alice.sent.size() == sent);
    runtime.now = UINT32_MAX - 1000;
    snapshot.in_team = true;
    presence.update(snapshot, alice.self, true);
    runtime.now = 6000;
    presence.update(snapshot, alice.self, true);
    assert(alice.sent.size() == sent + 1);
    a.clearKeys();
    runtime.now += 120000;
    presence.update(snapshot, alice.self, true);
    assert(alice.sent.size() == sent + 1);
}

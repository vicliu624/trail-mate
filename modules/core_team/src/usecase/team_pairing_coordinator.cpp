#include "team/usecase/team_pairing_coordinator.h"

#include "team/protocol/team_pairing_wire.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace team
{
namespace
{
constexpr uint32_t kLeaderWindowMs = 120000;
constexpr uint32_t kMemberTimeoutMs = 30000;
constexpr uint32_t kBeaconIntervalMs = 600;
constexpr uint32_t kJoinRetryMs = 1500;
constexpr uint8_t kJoinRetryMax = 6;
constexpr uint32_t kJoinSentHoldMs = 800;

void format_mac(const uint8_t* mac, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (!mac)
    {
        out[0] = '\0';
        return;
    }
    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void copy_cstr_trunc(char* dst, size_t dst_len, const char* src)
{
    if (!dst || dst_len == 0)
    {
        return;
    }

    if (!src)
    {
        dst[0] = '\0';
        return;
    }

    const size_t src_len = strnlen(src, dst_len - 1);
    if (src_len > 0)
    {
        memcpy(dst, src, src_len);
    }
    dst[src_len] = '\0';
}

} // namespace

TeamPairingCoordinator::TeamPairingCoordinator(team::ITeamRuntime& runtime,
                                               team::ITeamPairingEventSink& event_sink,
                                               team::ITeamPairingTransport& transport)
    : runtime_(runtime), event_sink_(event_sink), transport_(transport)
{
    team_psk_.fill(0);
}

uint32_t TeamPairingCoordinator::nextRandomU32()
{
    uint8_t bytes[sizeof(uint32_t)] = {0};
    runtime_.fillRandomBytes(bytes, sizeof(bytes));
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

void TeamPairingCoordinator::copyTeamName(const char* name)
{
    if (!name || name[0] == '\0')
    {
        team_name_[0] = '\0';
        has_team_name_ = false;
        return;
    }
    copy_cstr_trunc(team_name_, sizeof(team_name_), name);
    has_team_name_ = true;
}

void TeamPairingCoordinator::setState(TeamPairingState state, uint32_t peer_id)
{
    state_ = state;
    state_since_ms_ = runtime_.nowMillis();
    publishState(peer_id);
}

void TeamPairingCoordinator::publishState(uint32_t peer_id) const
{
    team::TeamPairingEvent ev{};
    ev.role = role_;
    ev.state = state_;
    ev.peer_id = peer_id;
    ev.key_id = key_id_;
    if (has_team_id_)
    {
        ev.team_id = team_id_;
        ev.has_team_id = true;
    }
    if (has_team_name_)
    {
        copy_cstr_trunc(ev.team_name, sizeof(ev.team_name), team_name_);
        ev.has_team_name = true;
    }
    event_sink_.onTeamPairingStateChanged(ev);
}

bool TeamPairingCoordinator::startLeader(const TeamId& team_id,
                                         uint32_t key_id,
                                         const uint8_t* psk,
                                         size_t psk_len,
                                         uint32_t leader_id,
                                         const char* team_name)
{
    if (!psk || psk_len == 0)
    {
        return false;
    }

    role_ = TeamPairingRole::Leader;
    team_id_ = team_id;
    has_team_id_ = true;
    key_id_ = key_id;
    leader_id_ = leader_id;
    copyTeamName(team_name);

    team_psk_.fill(0);
    team_psk_len_ = static_cast<uint8_t>(psk_len > team_psk_.size() ? team_psk_.size() : psk_len);
    if (team_psk_len_ > 0)
    {
        memcpy(team_psk_.data(), psk, team_psk_len_);
    }

    active_until_ms_ = runtime_.nowMillis() + kLeaderWindowMs;
    last_beacon_ms_ = 0;
    join_attempts_ = 0;
    join_sent_ms_ = 0;
    setState(TeamPairingState::LeaderBeacon, 0);
    return true;
}

bool TeamPairingCoordinator::startMember(uint32_t self_id)
{
    role_ = TeamPairingRole::Member;
    state_ = TeamPairingState::MemberScanning;
    state_since_ms_ = runtime_.nowMillis();
    member_id_ = self_id;
    leader_id_ = 0;
    leader_mac_valid_ = false;
    has_team_id_ = false;
    key_id_ = 0;
    copyTeamName(nullptr);
    join_nonce_ = nextRandomU32();
    active_until_ms_ = runtime_.nowMillis() + kMemberTimeoutMs;
    last_join_ms_ = 0;
    join_attempts_ = 0;
    join_sent_ms_ = 0;
    publishState(0);
    return true;
}

void TeamPairingCoordinator::stop()
{
    role_ = TeamPairingRole::None;
    state_ = TeamPairingState::Idle;
    state_since_ms_ = runtime_.nowMillis();
    publishState(0);
}

TeamPairingStatus TeamPairingCoordinator::getStatus() const
{
    TeamPairingStatus status{};
    status.role = role_;
    status.state = state_;
    status.team_id = team_id_;
    status.has_team_id = has_team_id_;
    status.key_id = key_id_;
    status.peer_id = 0;
    if (has_team_name_)
    {
        copy_cstr_trunc(status.team_name, sizeof(status.team_name), team_name_);
        status.has_team_name = true;
    }
    return status;
}

void TeamPairingCoordinator::handleIncomingPacket(const uint8_t* mac, const uint8_t* data, size_t len)
{
    if (!mac || !data || len == 0)
    {
        return;
    }

    team::proto::pairing::MessageType type = team::proto::pairing::MessageType::Beacon;
    if (!team::proto::pairing::decodeType(data, len, &type))
    {
        return;
    }

    switch (type)
    {
    case team::proto::pairing::MessageType::Beacon:
        handleBeacon(mac, data, len);
        break;
    case team::proto::pairing::MessageType::Join:
        handleJoin(mac, data, len);
        break;
    case team::proto::pairing::MessageType::Key:
        handleKey(mac, data, len);
        break;
    default:
        break;
    }
}

void TeamPairingCoordinator::handleBeacon(const uint8_t* mac, const uint8_t* data, size_t len)
{
    if (role_ != TeamPairingRole::Member ||
        (state_ != TeamPairingState::MemberScanning && state_ != TeamPairingState::JoinSent &&
         state_ != TeamPairingState::WaitingKey))
    {
        return;
    }

    team::proto::pairing::BeaconPacket packet{};
    if (!team::proto::pairing::decodeBeacon(data, len, &packet))
    {
        return;
    }

    uint32_t now = runtime_.nowMillis();
    team_id_ = packet.team_id;
    has_team_id_ = true;
    key_id_ = packet.key_id;
    leader_id_ = packet.leader_id;
    if (packet.has_team_name)
    {
        copyTeamName(packet.team_name);
    }

    memcpy(leader_mac_, mac, 6);
    leader_mac_valid_ = true;
    join_nonce_ = nextRandomU32();
    join_attempts_ = 0;
    sendJoin();
    join_sent_ms_ = now;
    setState(TeamPairingState::JoinSent, 0);
}

void TeamPairingCoordinator::handleJoin(const uint8_t* mac, const uint8_t* data, size_t len)
{
    if (role_ != TeamPairingRole::Leader || state_ != TeamPairingState::LeaderBeacon)
    {
        return;
    }

    team::proto::pairing::JoinPacket packet{};
    if (!team::proto::pairing::decodeJoin(data, len, &packet))
    {
        return;
    }
    if (!has_team_id_ || packet.team_id != team_id_)
    {
        return;
    }

    bool ok = sendKey(mac, packet.member_id, packet.nonce);
    if (ok)
    {
        publishState(packet.member_id);
    }
}

void TeamPairingCoordinator::handleKey(const uint8_t* mac, const uint8_t* data, size_t len)
{
    (void)mac;
    if (role_ != TeamPairingRole::Member ||
        (state_ != TeamPairingState::WaitingKey && state_ != TeamPairingState::JoinSent))
    {
        return;
    }

    team::proto::pairing::KeyPacket packet{};
    if (!team::proto::pairing::decodeKey(data, len, &packet))
    {
        return;
    }
    if (!has_team_id_ || packet.team_id != team_id_ || packet.nonce != join_nonce_)
    {
        return;
    }

    team_psk_.fill(0);
    if (packet.channel_psk_len > 0)
    {
        memcpy(team_psk_.data(), packet.channel_psk.data(), packet.channel_psk_len);
    }
    team_psk_len_ = packet.channel_psk_len;
    key_id_ = packet.key_id;

    team::proto::TeamKeyDist msg{};
    msg.team_id = team_id_;
    msg.key_id = key_id_;
    msg.channel_psk_len = team_psk_len_;
    msg.channel_psk = team_psk_;

    team::TeamKeyDistEvent ev{};
    ev.ctx.team_id = team_id_;
    ev.ctx.key_id = key_id_;
    ev.ctx.from = leader_id_;
    ev.ctx.timestamp = runtime_.nowUnixSeconds();
    ev.msg = msg;
    event_sink_.onTeamPairingKeyDist(ev);

    setState(TeamPairingState::Completed, leader_id_);
    stop();
}

void TeamPairingCoordinator::sendBeacon()
{
    team::proto::pairing::BeaconPacket packet{};
    packet.team_id = team_id_;
    packet.key_id = key_id_;
    packet.leader_id = leader_id_;
    packet.window_ms = kLeaderWindowMs;
    if (has_team_name_)
    {
        copy_cstr_trunc(packet.team_name, sizeof(packet.team_name), team_name_);
        packet.has_team_name = true;
    }

    std::vector<uint8_t> wire;
    if (!team::proto::pairing::encodeBeacon(packet, wire))
    {
        return;
    }

    static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (!transport_.ensurePeer(broadcast_mac))
    {
        return;
    }
    if (!transport_.send(broadcast_mac, wire.data(), wire.size()))
    {
        return;
    }
    last_beacon_ms_ = runtime_.nowMillis();
}

void TeamPairingCoordinator::sendJoin()
{
    if (!leader_mac_valid_)
    {
        return;
    }

    team::proto::pairing::JoinPacket packet{};
    packet.team_id = team_id_;
    packet.member_id = member_id_;
    packet.nonce = join_nonce_;

    std::vector<uint8_t> wire;
    if (!team::proto::pairing::encodeJoin(packet, wire))
    {
        return;
    }

    if (!transport_.ensurePeer(leader_mac_))
    {
        return;
    }
    if (!transport_.send(leader_mac_, wire.data(), wire.size()))
    {
        return;
    }
    last_join_ms_ = runtime_.nowMillis();
    if (join_attempts_ < 255)
    {
        join_attempts_++;
    }
}

bool TeamPairingCoordinator::sendKey(const uint8_t* mac, uint32_t member_id, uint32_t nonce)
{
    (void)member_id;
    if (!mac || team_psk_len_ == 0)
    {
        return false;
    }

    team::proto::pairing::KeyPacket packet{};
    packet.team_id = team_id_;
    packet.key_id = key_id_;
    packet.nonce = nonce;
    packet.channel_psk = team_psk_;
    packet.channel_psk_len = team_psk_len_;

    std::vector<uint8_t> wire;
    if (!team::proto::pairing::encodeKey(packet, wire))
    {
        return false;
    }

    if (!transport_.ensurePeer(mac))
    {
        return false;
    }
    return transport_.send(mac, wire.data(), wire.size());
}

void TeamPairingCoordinator::update()
{
    uint32_t now = runtime_.nowMillis();
    if (state_ == TeamPairingState::LeaderBeacon)
    {
        if (active_until_ms_ != 0 && now >= active_until_ms_)
        {
            setState(TeamPairingState::Failed, 0);
            stop();
            return;
        }
        if (now - last_beacon_ms_ >= kBeaconIntervalMs)
        {
            sendBeacon();
        }
    }
    else if (state_ == TeamPairingState::MemberScanning)
    {
        if (active_until_ms_ != 0 && now >= active_until_ms_)
        {
            setState(TeamPairingState::Failed, 0);
            stop();
            return;
        }
    }
    else if (state_ == TeamPairingState::JoinSent)
    {
        if ((now - join_sent_ms_) >= kJoinSentHoldMs)
        {
            setState(TeamPairingState::WaitingKey, 0);
        }
        if (active_until_ms_ != 0 && now >= active_until_ms_)
        {
            setState(TeamPairingState::Failed, 0);
            stop();
            return;
        }
    }
    else if (state_ == TeamPairingState::WaitingKey)
    {
        if (active_until_ms_ != 0 && now >= active_until_ms_)
        {
            setState(TeamPairingState::Failed, 0);
            stop();
            return;
        }
        if (leader_mac_valid_ &&
            (now - last_join_ms_) >= kJoinRetryMs &&
            join_attempts_ < kJoinRetryMax)
        {
            sendJoin();
        }
    }
}

} // namespace team

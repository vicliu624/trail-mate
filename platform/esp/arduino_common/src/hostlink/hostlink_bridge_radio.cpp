#include "platform/esp/arduino_common/hostlink/hostlink_bridge_radio.h"

#include "app/app_facade_access.h"
#include "chat/domain/chat_types.h"
#include "chat/usecase/chat_service.h"
#include "hostlink/hostlink_app_data_codec.h"
#include "hostlink/hostlink_event_codec.h"
#include "hostlink/hostlink_types.h"
#include "platform/esp/arduino_common/hostlink/hostlink_service.h"
#include "platform/ui/team_ui_store_runtime.h"
#include "team/protocol/team_chat.h"
#include "team/protocol/team_mgmt.h"
#include "team/protocol/team_portnum.h"
#include "team/protocol/team_waypoint.h"

#include <algorithm>
#include <string>
#include <vector>

namespace hostlink::bridge
{

namespace
{
constexpr uint8_t kAppFlagTeamMeta = 1 << 0;
constexpr uint8_t kAppFlagWantResponse = 1 << 1;
constexpr uint8_t kAppFlagWasEncrypted = 1 << 2;

uint32_t s_team_state_hash = 0;
bool s_team_state_has_hash = false;
AppRxStats s_app_rx_stats{};
bool s_runtime_team_state_inited = false;
team::ui::TeamUiSnapshot s_runtime_team_state{};

bool team_id_has_value(const team::TeamId& id)
{
    for (uint8_t b : id)
    {
        if (b != 0)
        {
            return true;
        }
    }
    return false;
}

void ensure_runtime_team_state_loaded()
{
    if (s_runtime_team_state_inited)
    {
        return;
    }
    s_runtime_team_state_inited = true;
}

int find_runtime_member_index(uint32_t node_id)
{
    for (size_t i = 0; i < s_runtime_team_state.members.size(); ++i)
    {
        if (s_runtime_team_state.members[i].node_id == node_id)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void touch_runtime_member(uint32_t node_id, bool leader, uint32_t last_seen_s)
{
    if (node_id == 0)
    {
        return;
    }

    int idx = find_runtime_member_index(node_id);
    if (idx < 0)
    {
        team::ui::TeamMemberUi member{};
        member.node_id = node_id;
        member.online = true;
        member.leader = leader;
        member.last_seen_s = last_seen_s;
        s_runtime_team_state.members.push_back(member);
        return;
    }

    auto& member = s_runtime_team_state.members[static_cast<size_t>(idx)];
    member.online = true;
    member.leader = leader;
    member.last_seen_s = last_seen_s;
}

void update_runtime_team_context(const team::TeamEventContext& ctx, uint32_t timestamp_s)
{
    ensure_runtime_team_state_loaded();

    if (team_id_has_value(ctx.team_id))
    {
        s_runtime_team_state.team_id = ctx.team_id;
        s_runtime_team_state.has_team_id = true;
        s_runtime_team_state.in_team = true;
    }
    if (ctx.key_id != 0)
    {
        s_runtime_team_state.security_round = ctx.key_id;
    }
    if (timestamp_s != 0)
    {
        s_runtime_team_state.last_update_s = timestamp_s;
    }
}

void update_app_rx_stats(const chat::RxMeta* rx_meta)
{
    s_app_rx_stats.total++;
    if (!rx_meta)
    {
        return;
    }
    if (rx_meta->from_is)
    {
        s_app_rx_stats.from_is++;
    }
    if (rx_meta->hop_count != 0xFF)
    {
        if (rx_meta->direct)
        {
            s_app_rx_stats.direct++;
        }
        else
        {
            s_app_rx_stats.relayed++;
        }
    }
}

hostlink::TeamStateSnapshot make_team_state_snapshot()
{
    ensure_runtime_team_state_loaded();
    const team::ui::TeamUiSnapshot& snap = s_runtime_team_state;

    hostlink::TeamStateSnapshot snapshot;
    snapshot.in_team = snap.in_team;
    snapshot.pending_join = snap.pending_join;
    snapshot.kicked_out = snap.kicked_out;
    snapshot.self_is_leader = snap.self_is_leader;
    snapshot.has_team_id = snap.has_team_id;
    snapshot.self_id = app::messagingFacade().getSelfNodeId();
    snapshot.team_id = snap.team_id.data();
    snapshot.team_id_len = snap.team_id.size();
    snapshot.security_round = snap.security_round;
    snapshot.last_event_seq = snap.last_event_seq;
    snapshot.last_update_s = snap.last_update_s;
    snapshot.team_name = snap.team_name;
    snapshot.members.reserve(snap.members.size());

    for (const auto& member : snap.members)
    {
        hostlink::TeamStateMemberSnapshot item;
        item.node_id = member.node_id;
        item.leader = member.leader;
        item.online = member.online;
        item.last_seen_s = member.last_seen_s;
        item.name = member.name;
        snapshot.members.push_back(std::move(item));
    }

    return snapshot;
}
void maybe_send_team_state(bool force)
{
    std::vector<uint8_t> payload;
    const hostlink::TeamStateSnapshot snapshot = make_team_state_snapshot();
    if (!hostlink::build_team_state_payload(snapshot, payload))
    {
        return;
    }
    uint32_t hash = hostlink::hash_bytes(payload.data(), payload.size());
    if (!force && s_team_state_has_hash && hash == s_team_state_hash)
    {
        return;
    }
    s_team_state_has_hash = true;
    s_team_state_hash = hash;
    hostlink::enqueue_event(static_cast<uint8_t>(hostlink::FrameType::EvTeamState),
                            payload.data(), payload.size(), false);
}

void send_app_data(uint32_t portnum,
                   uint32_t from,
                   uint32_t to,
                   uint8_t channel,
                   uint8_t flags,
                   const uint8_t* team_id,
                   uint32_t team_key_id,
                   uint32_t timestamp_s,
                   uint32_t packet_id,
                   const chat::RxMeta* rx_meta,
                   const uint8_t* payload,
                   size_t payload_len)
{
    hostlink::AppDataFrameEncodeRequest request;
    request.portnum = portnum;
    request.from = from;
    request.to = to;
    request.channel = channel;
    request.flags = flags;
    request.team_id = team_id;
    request.team_key_id = team_key_id;
    request.timestamp_s = timestamp_s;
    request.packet_id = packet_id;
    request.rx_meta = rx_meta;
    request.payload = payload;
    request.payload_len = payload_len;

    std::vector<std::vector<uint8_t>> frames;
    if (!hostlink::encode_app_data_frames(request,
                                          hostlink::kMaxFrameLen,
                                          team::proto::kTeamIdSize,
                                          frames))
    {
        return;
    }

    update_app_rx_stats(rx_meta);
    for (const auto& frame : frames)
    {
        hostlink::enqueue_event(static_cast<uint8_t>(hostlink::FrameType::EvAppData),
                                frame.data(), frame.size(), false);
    }
}

template <typename Msg, bool (*Encoder)(const Msg&, std::vector<uint8_t>&)>
bool encode_team_mgmt_wire(team::proto::TeamMgmtType type, const Msg& msg, std::vector<uint8_t>& out)
{
    std::vector<uint8_t> payload;
    if (!Encoder(msg, payload))
    {
        return false;
    }
    return team::proto::encodeTeamMgmtMessage(type, payload, out);
}
} // namespace

void on_event(const sys::Event& event)
{
    if (!hostlink::is_active())
    {
        return;
    }
    hostlink::Status st = hostlink::get_status();
    if (st.state != hostlink::LinkState::Ready)
    {
        return;
    }

    switch (event.type)
    {
    case sys::EventType::ChatNewMessage:
    {
        const auto& msg_evt = static_cast<const sys::ChatNewMessageEvent&>(event);
        const chat::ChatMessage* msg =
            app::messagingFacade().getChatService().getMessage(msg_evt.msg_id);

        uint32_t msg_id = msg ? msg->msg_id : msg_evt.msg_id;
        uint32_t from = msg ? msg->from : 0;
        uint32_t to = msg ? msg->peer : 0;
        uint8_t channel = msg ? static_cast<uint8_t>(msg->channel) : msg_evt.channel;
        uint32_t ts = msg ? msg->timestamp : 0;
        const std::string text = msg ? msg->text : std::string(msg_evt.text);

        std::vector<uint8_t> meta_tlv;
        hostlink::build_rx_meta_tlvs(msg_evt.rx_meta, msg_id, meta_tlv);
        update_app_rx_stats(&msg_evt.rx_meta);

        hostlink::RxMessageEventPayload snapshot;
        snapshot.msg_id = msg_id;
        snapshot.from = from;
        snapshot.to = to;
        snapshot.channel = channel;
        snapshot.timestamp_s = ts;
        snapshot.text = text;
        snapshot.meta_tlv = meta_tlv.empty() ? nullptr : meta_tlv.data();
        snapshot.meta_tlv_len = meta_tlv.size();

        std::vector<uint8_t> payload;
        if (!hostlink::build_rx_message_payload(snapshot, payload))
        {
            break;
        }

        hostlink::enqueue_event(static_cast<uint8_t>(hostlink::FrameType::EvRxMsg),
                                payload.data(), payload.size(), false);
        break;
    }
    case sys::EventType::ChatSendResult:
    {
        const auto& res_evt = static_cast<const sys::ChatSendResultEvent&>(event);
        std::vector<uint8_t> payload;
        if (!hostlink::build_tx_result_payload(res_evt.msg_id, res_evt.success, payload))
        {
            break;
        }
        hostlink::enqueue_event(static_cast<uint8_t>(hostlink::FrameType::EvTxResult),
                                payload.data(), payload.size(), true);
        break;
    }
    case sys::EventType::AppData:
    {
        const auto& data_evt = static_cast<const sys::AppDataEvent&>(event);
        uint8_t flags = 0;
        if (data_evt.want_response)
        {
            flags |= kAppFlagWantResponse;
        }
        send_app_data(data_evt.portnum,
                      data_evt.from,
                      data_evt.to,
                      data_evt.channel,
                      flags,
                      nullptr,
                      0,
                      event.timestamp / 1000,
                      data_evt.packet_id,
                      &data_evt.rx_meta,
                      data_evt.payload.data(),
                      data_evt.payload.size());
        break;
    }
    case sys::EventType::TeamKick:
    {
        const auto& team_evt = static_cast<const sys::TeamKickEvent&>(event);
        uint32_t ts_s = event.timestamp / 1000;
        update_runtime_team_context(team_evt.data.ctx, ts_s);
        if (team_evt.data.msg.target != 0)
        {
            int idx = find_runtime_member_index(team_evt.data.msg.target);
            if (idx >= 0)
            {
                s_runtime_team_state.members.erase(s_runtime_team_state.members.begin() + idx);
            }
            uint32_t self_id = app::messagingFacade().getSelfNodeId();
            if (self_id != 0 && team_evt.data.msg.target == self_id)
            {
                s_runtime_team_state.in_team = false;
                s_runtime_team_state.pending_join = false;
                s_runtime_team_state.kicked_out = true;
                s_runtime_team_state.members.clear();
            }
        }

        std::vector<uint8_t> wire;
        if (encode_team_mgmt_wire<team::proto::TeamKick, team::proto::encodeTeamKick>(
                team::proto::TeamMgmtType::Kick, team_evt.data.msg, wire))
        {
            uint8_t flags = kAppFlagTeamMeta;
            if (team_evt.data.ctx.key_id != 0)
            {
                flags |= kAppFlagWasEncrypted;
            }
            send_app_data(team::proto::TEAM_MGMT_APP,
                          team_evt.data.ctx.from,
                          0,
                          0,
                          flags,
                          team_evt.data.ctx.team_id.data(),
                          team_evt.data.ctx.key_id,
                          event.timestamp / 1000,
                          0,
                          &team_evt.data.ctx.rx_meta,
                          wire.data(),
                          wire.size());
        }
        break;
    }
    case sys::EventType::TeamTransferLeader:
    {
        const auto& team_evt = static_cast<const sys::TeamTransferLeaderEvent&>(event);
        uint32_t ts_s = event.timestamp / 1000;
        update_runtime_team_context(team_evt.data.ctx, ts_s);
        for (auto& member : s_runtime_team_state.members)
        {
            member.leader = false;
        }
        if (team_evt.data.msg.target != 0)
        {
            touch_runtime_member(team_evt.data.msg.target, true, ts_s);
        }
        uint32_t self_id = app::messagingFacade().getSelfNodeId();
        s_runtime_team_state.self_is_leader = (self_id != 0 && team_evt.data.msg.target == self_id);

        std::vector<uint8_t> wire;
        if (encode_team_mgmt_wire<team::proto::TeamTransferLeader, team::proto::encodeTeamTransferLeader>(
                team::proto::TeamMgmtType::TransferLeader, team_evt.data.msg, wire))
        {
            uint8_t flags = kAppFlagTeamMeta;
            if (team_evt.data.ctx.key_id != 0)
            {
                flags |= kAppFlagWasEncrypted;
            }
            send_app_data(team::proto::TEAM_MGMT_APP,
                          team_evt.data.ctx.from,
                          0,
                          0,
                          flags,
                          team_evt.data.ctx.team_id.data(),
                          team_evt.data.ctx.key_id,
                          event.timestamp / 1000,
                          0,
                          &team_evt.data.ctx.rx_meta,
                          wire.data(),
                          wire.size());
        }
        break;
    }
    case sys::EventType::TeamKeyDist:
    {
        const auto& team_evt = static_cast<const sys::TeamKeyDistEvent&>(event);
        update_runtime_team_context(team_evt.data.ctx, event.timestamp / 1000);
        if (team_evt.data.msg.key_id != 0)
        {
            s_runtime_team_state.security_round = team_evt.data.msg.key_id;
        }

        std::vector<uint8_t> wire;
        if (encode_team_mgmt_wire<team::proto::TeamKeyDist, team::proto::encodeTeamKeyDist>(
                team::proto::TeamMgmtType::KeyDist, team_evt.data.msg, wire))
        {
            uint8_t flags = kAppFlagTeamMeta;
            if (team_evt.data.ctx.key_id != 0)
            {
                flags |= kAppFlagWasEncrypted;
            }
            send_app_data(team::proto::TEAM_MGMT_APP,
                          team_evt.data.ctx.from,
                          0,
                          0,
                          flags,
                          team_evt.data.ctx.team_id.data(),
                          team_evt.data.ctx.key_id,
                          event.timestamp / 1000,
                          0,
                          &team_evt.data.ctx.rx_meta,
                          wire.data(),
                          wire.size());
        }
        break;
    }
    case sys::EventType::TeamStatus:
    {
        const auto& team_evt = static_cast<const sys::TeamStatusEvent&>(event);
        uint32_t ts_s = event.timestamp / 1000;
        update_runtime_team_context(team_evt.data.ctx, ts_s);
        if (team_evt.data.msg.key_id != 0)
        {
            s_runtime_team_state.security_round = team_evt.data.msg.key_id;
        }
        if (team_evt.data.msg.has_members)
        {
            s_runtime_team_state.members.clear();
            for (uint32_t member_id : team_evt.data.msg.members)
            {
                if (member_id == 0)
                {
                    continue;
                }
                bool leader = (team_evt.data.msg.leader_id != 0 && member_id == team_evt.data.msg.leader_id);
                touch_runtime_member(member_id, leader, ts_s);
            }
        }
        if (team_evt.data.ctx.from != 0)
        {
            bool from_is_leader = (team_evt.data.msg.leader_id != 0 && team_evt.data.ctx.from == team_evt.data.msg.leader_id);
            touch_runtime_member(team_evt.data.ctx.from, from_is_leader, ts_s);
        }
        uint32_t self_id = app::messagingFacade().getSelfNodeId();
        if (self_id != 0 && team_evt.data.msg.leader_id != 0)
        {
            s_runtime_team_state.self_is_leader = (team_evt.data.msg.leader_id == self_id);
        }

        std::vector<uint8_t> wire;
        if (encode_team_mgmt_wire<team::proto::TeamStatus, team::proto::encodeTeamStatus>(
                team::proto::TeamMgmtType::Status, team_evt.data.msg, wire))
        {
            uint8_t flags = kAppFlagTeamMeta;
            if (team_evt.data.ctx.key_id != 0)
            {
                flags |= kAppFlagWasEncrypted;
            }
            send_app_data(team::proto::TEAM_MGMT_APP,
                          team_evt.data.ctx.from,
                          0,
                          0,
                          flags,
                          team_evt.data.ctx.team_id.data(),
                          team_evt.data.ctx.key_id,
                          event.timestamp / 1000,
                          0,
                          &team_evt.data.ctx.rx_meta,
                          wire.data(),
                          wire.size());
        }
        break;
    }
    case sys::EventType::TeamPosition:
    {
        const auto& team_evt = static_cast<const sys::TeamPositionEvent&>(event);
        uint32_t ts_s = event.timestamp / 1000;
        update_runtime_team_context(team_evt.data.ctx, ts_s);
        touch_runtime_member(team_evt.data.ctx.from, false, ts_s);

        uint8_t flags = kAppFlagTeamMeta | kAppFlagWasEncrypted;
        send_app_data(team::proto::TEAM_POSITION_APP,
                      team_evt.data.ctx.from,
                      0,
                      0,
                      flags,
                      team_evt.data.ctx.team_id.data(),
                      team_evt.data.ctx.key_id,
                      event.timestamp / 1000,
                      0,
                      &team_evt.data.ctx.rx_meta,
                      team_evt.data.payload.data(),
                      team_evt.data.payload.size());
        break;
    }
    case sys::EventType::TeamWaypoint:
    {
        const auto& team_evt = static_cast<const sys::TeamWaypointEvent&>(event);
        uint32_t ts_s = event.timestamp / 1000;
        update_runtime_team_context(team_evt.data.ctx, ts_s);
        touch_runtime_member(team_evt.data.ctx.from, false, ts_s);

        std::vector<uint8_t> payload;
        if (!team::proto::encodeTeamWaypointMessage(team_evt.data.msg, payload))
        {
            break;
        }
        uint8_t flags = kAppFlagTeamMeta | kAppFlagWasEncrypted;
        send_app_data(team::proto::TEAM_WAYPOINT_APP,
                      team_evt.data.ctx.from,
                      0,
                      0,
                      flags,
                      team_evt.data.ctx.team_id.data(),
                      team_evt.data.ctx.key_id,
                      event.timestamp / 1000,
                      0,
                      &team_evt.data.ctx.rx_meta,
                      payload.data(),
                      payload.size());
        break;
    }
    case sys::EventType::TeamTrack:
    {
        const auto& team_evt = static_cast<const sys::TeamTrackEvent&>(event);
        uint32_t ts_s = event.timestamp / 1000;
        update_runtime_team_context(team_evt.data.ctx, ts_s);
        touch_runtime_member(team_evt.data.ctx.from, false, ts_s);

        uint8_t flags = kAppFlagTeamMeta | kAppFlagWasEncrypted;
        send_app_data(team::proto::TEAM_TRACK_APP,
                      team_evt.data.ctx.from,
                      0,
                      0,
                      flags,
                      team_evt.data.ctx.team_id.data(),
                      team_evt.data.ctx.key_id,
                      event.timestamp / 1000,
                      0,
                      &team_evt.data.ctx.rx_meta,
                      team_evt.data.payload.data(),
                      team_evt.data.payload.size());
        break;
    }
    case sys::EventType::TeamChat:
    {
        const auto& team_evt = static_cast<const sys::TeamChatEvent&>(event);
        uint32_t ts_s = event.timestamp / 1000;
        update_runtime_team_context(team_evt.data.ctx, ts_s);
        touch_runtime_member(team_evt.data.ctx.from, false, ts_s);

        std::vector<uint8_t> wire;
        if (team::proto::encodeTeamChatMessage(team_evt.data.msg, wire))
        {
            uint8_t flags = kAppFlagTeamMeta | kAppFlagWasEncrypted;
            send_app_data(team::proto::TEAM_CHAT_APP,
                          team_evt.data.ctx.from,
                          0,
                          0,
                          flags,
                          team_evt.data.ctx.team_id.data(),
                          team_evt.data.ctx.key_id,
                          event.timestamp / 1000,
                          0,
                          &team_evt.data.ctx.rx_meta,
                          wire.data(),
                          wire.size());
        }
        break;
    }
    case sys::EventType::TeamPairing:
    {
        const auto& pair_evt = static_cast<const sys::TeamPairingEvent&>(event);
        ensure_runtime_team_state_loaded();
        if (pair_evt.data.has_team_id)
        {
            s_runtime_team_state.team_id = pair_evt.data.team_id;
            s_runtime_team_state.has_team_id = true;
            s_runtime_team_state.in_team = true;
        }
        if (pair_evt.data.key_id != 0)
        {
            s_runtime_team_state.security_round = pair_evt.data.key_id;
        }
        if (pair_evt.data.has_team_name && pair_evt.data.team_name[0] != '\0')
        {
            s_runtime_team_state.team_name = pair_evt.data.team_name;
        }
        if (pair_evt.data.state == team::TeamPairingState::Completed)
        {
            s_runtime_team_state.in_team = true;
            s_runtime_team_state.pending_join = false;
            s_runtime_team_state.kicked_out = false;
        }
        break;
    }
    default:
        break;
    }

    switch (event.type)
    {
    case sys::EventType::TeamKick:
    case sys::EventType::TeamTransferLeader:
    case sys::EventType::TeamKeyDist:
    case sys::EventType::TeamStatus:
    case sys::EventType::TeamPosition:
    case sys::EventType::TeamWaypoint:
    case sys::EventType::TeamTrack:
    case sys::EventType::TeamChat:
    case sys::EventType::TeamPairing:
        maybe_send_team_state(false);
        break;
    default:
        break;
    }
}

void on_link_ready()
{
    if (!hostlink::is_active())
    {
        return;
    }
    maybe_send_team_state(true);
}

AppRxStats get_app_rx_stats()
{
    return s_app_rx_stats;
}

} // namespace hostlink::bridge

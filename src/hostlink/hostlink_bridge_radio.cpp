#include "hostlink_bridge_radio.h"

#include "../app/app_context.h"
#include "../chat/domain/chat_types.h"
#include "../team/protocol/team_chat.h"
#include "../team/protocol/team_mgmt.h"
#include "../team/protocol/team_portnum.h"
#include "../ui/screens/team/team_ui_store.h"
#include "hostlink_service.h"
#include "hostlink_types.h"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

namespace hostlink::bridge
{

namespace
{
constexpr uint8_t kAppFlagTeamMeta = 1 << 0;
constexpr uint8_t kAppFlagWantResponse = 1 << 1;
constexpr uint8_t kAppFlagWasEncrypted = 1 << 2;
constexpr uint8_t kAppFlagMoreChunks = 1 << 3;
constexpr size_t kAppDataHeaderSize = 4 + 4 + 4 + 1 + 1 + team::proto::kTeamIdSize + 4 + 4 + 4 + 4 + 2;
constexpr uint8_t kTeamStateVersion = 1;
constexpr size_t kTeamNameMaxLen = 48;
constexpr size_t kMemberNameMaxLen = 32;

uint32_t s_team_state_hash = 0;
bool s_team_state_has_hash = false;
AppRxStats s_app_rx_stats{};
bool s_runtime_team_state_inited = false;
team::ui::TeamUiSnapshot s_runtime_team_state{};

void push_u8(std::vector<uint8_t>& out, uint8_t v)
{
    out.push_back(v);
}

void push_u16(std::vector<uint8_t>& out, uint16_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void push_u32(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void push_i16(std::vector<uint8_t>& out, int16_t v)
{
    push_u16(out, static_cast<uint16_t>(v));
}

void push_tlv(std::vector<uint8_t>& out, uint8_t key, const void* data, size_t len)
{
    if (!data || len == 0 || len > 255)
    {
        return;
    }
    out.push_back(key);
    out.push_back(static_cast<uint8_t>(len));
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + len);
}

void push_tlv_u8(std::vector<uint8_t>& out, uint8_t key, uint8_t value)
{
    push_tlv(out, key, &value, sizeof(value));
}

void push_tlv_u16(std::vector<uint8_t>& out, uint8_t key, uint16_t value)
{
    uint8_t buf[2] = {static_cast<uint8_t>(value & 0xFF),
                      static_cast<uint8_t>((value >> 8) & 0xFF)};
    push_tlv(out, key, buf, sizeof(buf));
}

void push_tlv_u32(std::vector<uint8_t>& out, uint8_t key, uint32_t value)
{
    uint8_t buf[4] = {static_cast<uint8_t>(value & 0xFF),
                      static_cast<uint8_t>((value >> 8) & 0xFF),
                      static_cast<uint8_t>((value >> 16) & 0xFF),
                      static_cast<uint8_t>((value >> 24) & 0xFF)};
    push_tlv(out, key, buf, sizeof(buf));
}

void push_tlv_i16(std::vector<uint8_t>& out, uint8_t key, int16_t value)
{
    uint8_t buf[2] = {static_cast<uint8_t>(value & 0xFF),
                      static_cast<uint8_t>((value >> 8) & 0xFF)};
    push_tlv(out, key, buf, sizeof(buf));
}

void push_bytes(std::vector<uint8_t>& out, const uint8_t* data, size_t len)
{
    if (!data || len == 0)
    {
        return;
    }
    out.insert(out.end(), data, data + len);
}

void push_zeros(std::vector<uint8_t>& out, size_t len)
{
    out.insert(out.end(), len, 0);
}

uint32_t hash_bytes(const uint8_t* data, size_t len)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; ++i)
    {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

void push_string(std::vector<uint8_t>& out, const std::string& value, size_t max_len)
{
    size_t len = value.size();
    if (len > max_len)
    {
        len = max_len;
    }
    push_u16(out, static_cast<uint16_t>(len));
    if (len > 0)
    {
        out.insert(out.end(), value.begin(), value.begin() + static_cast<std::ptrdiff_t>(len));
    }
}

void push_team_id(std::vector<uint8_t>& out, const team::TeamId& id, bool has_id)
{
    if (has_id)
    {
        push_bytes(out, id.data(), id.size());
    }
    else
    {
        push_zeros(out, id.size());
    }
}

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

void build_rx_meta_tlvs(const chat::RxMeta& meta, uint32_t packet_id, std::vector<uint8_t>& out)
{
    out.clear();
    if (meta.rx_timestamp_s != 0)
    {
        push_tlv_u32(out, static_cast<uint8_t>(hostlink::AppDataMetaKey::RxTimestampS),
                     meta.rx_timestamp_s);
    }
    if (meta.rx_timestamp_ms != 0)
    {
        push_tlv_u32(out, static_cast<uint8_t>(hostlink::AppDataMetaKey::RxTimestampMs),
                     meta.rx_timestamp_ms);
    }
    if (meta.time_source != chat::RxTimeSource::Unknown)
    {
        push_tlv_u8(out, static_cast<uint8_t>(hostlink::AppDataMetaKey::RxTimeSource),
                    static_cast<uint8_t>(meta.time_source));
    }
    if (meta.hop_count != 0xFF)
    {
        push_tlv_u8(out, static_cast<uint8_t>(hostlink::AppDataMetaKey::HopCount), meta.hop_count);
        push_tlv_u8(out, static_cast<uint8_t>(hostlink::AppDataMetaKey::Direct),
                    meta.direct ? 1 : 0);
    }
    if (meta.hop_limit != 0xFF)
    {
        push_tlv_u8(out, static_cast<uint8_t>(hostlink::AppDataMetaKey::HopLimit), meta.hop_limit);
    }
    if (meta.origin != chat::RxOrigin::Unknown)
    {
        push_tlv_u8(out, static_cast<uint8_t>(hostlink::AppDataMetaKey::RxOrigin),
                    static_cast<uint8_t>(meta.origin));
        push_tlv_u8(out, static_cast<uint8_t>(hostlink::AppDataMetaKey::FromIs),
                    meta.from_is ? 1 : 0);
    }
    if (meta.channel_hash != 0xFF)
    {
        push_tlv_u8(out, static_cast<uint8_t>(hostlink::AppDataMetaKey::ChannelHash),
                    meta.channel_hash);
    }
    if (meta.wire_flags != 0xFF)
    {
        push_tlv_u8(out, static_cast<uint8_t>(hostlink::AppDataMetaKey::WireFlags),
                    meta.wire_flags);
    }
    if (meta.next_hop != 0)
    {
        push_tlv_u32(out, static_cast<uint8_t>(hostlink::AppDataMetaKey::NextHop), meta.next_hop);
    }
    if (meta.relay_node != 0)
    {
        push_tlv_u32(out, static_cast<uint8_t>(hostlink::AppDataMetaKey::RelayNode), meta.relay_node);
    }
    if (meta.rssi_dbm_x10 != std::numeric_limits<int16_t>::min())
    {
        push_tlv_i16(out, static_cast<uint8_t>(hostlink::AppDataMetaKey::RssiDbmX10),
                     meta.rssi_dbm_x10);
    }
    if (meta.snr_db_x10 != std::numeric_limits<int16_t>::min())
    {
        push_tlv_i16(out, static_cast<uint8_t>(hostlink::AppDataMetaKey::SnrDbX10),
                     meta.snr_db_x10);
    }
    if (meta.freq_hz != 0)
    {
        push_tlv_u32(out, static_cast<uint8_t>(hostlink::AppDataMetaKey::FreqHz), meta.freq_hz);
    }
    if (meta.bw_hz != 0)
    {
        push_tlv_u32(out, static_cast<uint8_t>(hostlink::AppDataMetaKey::BwHz), meta.bw_hz);
    }
    if (meta.sf != 0)
    {
        push_tlv_u8(out, static_cast<uint8_t>(hostlink::AppDataMetaKey::Sf), meta.sf);
    }
    if (meta.cr != 0)
    {
        push_tlv_u8(out, static_cast<uint8_t>(hostlink::AppDataMetaKey::Cr), meta.cr);
    }
    if (packet_id != 0)
    {
        push_tlv_u32(out, static_cast<uint8_t>(hostlink::AppDataMetaKey::PacketId), packet_id);
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

bool build_team_state_payload(std::vector<uint8_t>& out)
{
    ensure_runtime_team_state_loaded();
    const team::ui::TeamUiSnapshot& snap = s_runtime_team_state;

    uint8_t flags = 0;
    if (snap.in_team) flags |= 1 << 0;
    if (snap.pending_join) flags |= 1 << 1;
    if (snap.kicked_out) flags |= 1 << 2;
    if (snap.self_is_leader) flags |= 1 << 3;
    if (snap.has_team_id) flags |= 1 << 4;

    const uint32_t self_id = app::AppContext::getInstance().getSelfNodeId();

    out.clear();
    out.reserve(128 + snap.members.size() * 32);
    push_u8(out, kTeamStateVersion);
    push_u8(out, flags);
    push_u16(out, 0);
    push_u32(out, self_id);
    push_team_id(out, snap.team_id, snap.has_team_id);
    push_zeros(out, team::proto::kTeamIdSize);
    push_u32(out, snap.security_round);
    push_u32(out, snap.last_event_seq);
    push_u32(out, snap.last_update_s);
    push_string(out, snap.team_name, kTeamNameMaxLen);

    size_t member_count = snap.members.size();
    if (member_count > 255)
    {
        member_count = 255;
    }
    push_u8(out, static_cast<uint8_t>(member_count));

    for (size_t i = 0; i < member_count; ++i)
    {
        const auto& member = snap.members[i];
        push_u32(out, member.node_id);
        push_u8(out, member.leader ? 1 : 0);
        push_u8(out, member.online ? 1 : 0);
        push_u32(out, member.last_seen_s);
        push_string(out, member.name, kMemberNameMaxLen);
    }

    return true;
}

void maybe_send_team_state(bool force)
{
    std::vector<uint8_t> payload;
    if (!build_team_state_payload(payload))
    {
        return;
    }
    uint32_t hash = hash_bytes(payload.data(), payload.size());
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
    std::vector<uint8_t> meta_tlv;
    if (rx_meta)
    {
        build_rx_meta_tlvs(*rx_meta, packet_id, meta_tlv);
    }
    const size_t meta_len = meta_tlv.size();

    if (kMaxFrameLen <= kAppDataHeaderSize + meta_len)
    {
        return;
    }
    update_app_rx_stats(rx_meta);
    const size_t max_chunk = kMaxFrameLen - kAppDataHeaderSize - meta_len;
    size_t offset = 0;
    if (payload_len == 0)
    {
        std::vector<uint8_t> out;
        out.reserve(kAppDataHeaderSize + meta_len);
        push_u32(out, portnum);
        push_u32(out, from);
        push_u32(out, to);
        push_u8(out, channel);
        push_u8(out, flags);
        if (team_id)
        {
            push_bytes(out, team_id, team::proto::kTeamIdSize);
        }
        else
        {
            push_zeros(out, team::proto::kTeamIdSize);
        }
        push_u32(out, team_key_id);
        push_u32(out, timestamp_s);
        push_u32(out, 0);
        push_u32(out, 0);
        push_u16(out, 0);
        if (!meta_tlv.empty())
        {
            push_bytes(out, meta_tlv.data(), meta_tlv.size());
        }
        hostlink::enqueue_event(static_cast<uint8_t>(hostlink::FrameType::EvAppData),
                                out.data(), out.size(), false);
        return;
    }

    while (offset < payload_len)
    {
        size_t chunk_len = payload_len - offset;
        if (chunk_len > max_chunk)
        {
            chunk_len = max_chunk;
        }
        uint8_t chunk_flags = flags;
        if (offset + chunk_len < payload_len)
        {
            chunk_flags |= kAppFlagMoreChunks;
        }

        std::vector<uint8_t> out;
        out.reserve(kAppDataHeaderSize + chunk_len + meta_len);
        push_u32(out, portnum);
        push_u32(out, from);
        push_u32(out, to);
        push_u8(out, channel);
        push_u8(out, chunk_flags);
        if (team_id)
        {
            push_bytes(out, team_id, team::proto::kTeamIdSize);
        }
        else
        {
            push_zeros(out, team::proto::kTeamIdSize);
        }
        push_u32(out, team_key_id);
        push_u32(out, timestamp_s);
        push_u32(out, static_cast<uint32_t>(payload_len));
        push_u32(out, static_cast<uint32_t>(offset));
        push_u16(out, static_cast<uint16_t>(chunk_len));
        push_bytes(out, payload + offset, chunk_len);
        if (!meta_tlv.empty())
        {
            push_bytes(out, meta_tlv.data(), meta_tlv.size());
        }

        hostlink::enqueue_event(static_cast<uint8_t>(hostlink::FrameType::EvAppData),
                                out.data(), out.size(), false);
        offset += chunk_len;
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
            app::AppContext::getInstance().getChatService().getMessage(msg_evt.msg_id);

        uint32_t msg_id = msg ? msg->msg_id : msg_evt.msg_id;
        uint32_t from = msg ? msg->from : 0;
        uint32_t to = msg ? msg->peer : 0;
        uint8_t channel = msg ? static_cast<uint8_t>(msg->channel) : msg_evt.channel;
        uint32_t ts = msg ? msg->timestamp : 0;
        const std::string text = msg ? msg->text : std::string(msg_evt.text);

        std::vector<uint8_t> meta_tlv;
        build_rx_meta_tlvs(msg_evt.rx_meta, msg_id, meta_tlv);
        update_app_rx_stats(&msg_evt.rx_meta);

        std::vector<uint8_t> payload;
        payload.reserve(16 + text.size() + meta_tlv.size());
        push_u32(payload, msg_id);
        push_u32(payload, from);
        push_u32(payload, to);
        payload.push_back(channel);
        push_u32(payload, ts);
        push_u16(payload, static_cast<uint16_t>(text.size()));
        payload.insert(payload.end(), text.begin(), text.end());
        if (!meta_tlv.empty())
        {
            push_bytes(payload, meta_tlv.data(), meta_tlv.size());
        }

        hostlink::enqueue_event(static_cast<uint8_t>(hostlink::FrameType::EvRxMsg),
                                payload.data(), payload.size(), false);
        break;
    }
    case sys::EventType::ChatSendResult:
    {
        const auto& res_evt = static_cast<const sys::ChatSendResultEvent&>(event);
        std::vector<uint8_t> payload;
        payload.reserve(8);
        push_u32(payload, res_evt.msg_id);
        payload.push_back(res_evt.success ? 1 : 0);
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
            uint32_t self_id = app::AppContext::getInstance().getSelfNodeId();
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
        uint32_t self_id = app::AppContext::getInstance().getSelfNodeId();
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
        uint32_t self_id = app::AppContext::getInstance().getSelfNodeId();
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
                      team_evt.data.payload.data(),
                      team_evt.data.payload.size());
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

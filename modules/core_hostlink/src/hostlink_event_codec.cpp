#include "hostlink/hostlink_event_codec.h"

#include <cstddef>

namespace hostlink
{

namespace
{

constexpr uint8_t kTeamStateVersion = 1;
constexpr size_t kTeamNameMaxLen = 48;
constexpr size_t kMemberNameMaxLen = 32;

void push_u8(std::vector<uint8_t>& out, uint8_t value)
{
    out.push_back(value);
}

void push_u16(std::vector<uint8_t>& out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void push_u32(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
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

} // namespace

bool build_rx_message_payload(const RxMessageEventPayload& snapshot,
                              std::vector<uint8_t>& out)
{
    out.clear();
    out.reserve(16 + snapshot.text.size() + snapshot.meta_tlv_len);
    push_u32(out, snapshot.msg_id);
    push_u32(out, snapshot.from);
    push_u32(out, snapshot.to);
    push_u8(out, snapshot.channel);
    push_u32(out, snapshot.timestamp_s);
    push_u16(out, static_cast<uint16_t>(snapshot.text.size()));
    out.insert(out.end(), snapshot.text.begin(), snapshot.text.end());
    if (snapshot.meta_tlv_len > 0)
    {
        push_bytes(out, snapshot.meta_tlv, snapshot.meta_tlv_len);
    }
    return true;
}

bool build_tx_result_payload(uint32_t msg_id, bool success, std::vector<uint8_t>& out)
{
    out.clear();
    out.reserve(8);
    push_u32(out, msg_id);
    push_u8(out, success ? 1 : 0);
    return true;
}

bool build_team_state_payload(const TeamStateSnapshot& snapshot,
                              std::vector<uint8_t>& out)
{
    uint8_t flags = 0;
    if (snapshot.in_team) flags |= 1 << 0;
    if (snapshot.pending_join) flags |= 1 << 1;
    if (snapshot.kicked_out) flags |= 1 << 2;
    if (snapshot.self_is_leader) flags |= 1 << 3;
    if (snapshot.has_team_id) flags |= 1 << 4;

    out.clear();
    out.reserve(128 + snapshot.members.size() * 32);
    push_u8(out, kTeamStateVersion);
    push_u8(out, flags);
    push_u16(out, 0);
    push_u32(out, snapshot.self_id);
    if (snapshot.has_team_id && snapshot.team_id && snapshot.team_id_len > 0)
    {
        push_bytes(out, snapshot.team_id, snapshot.team_id_len);
    }
    else
    {
        push_zeros(out, snapshot.team_id_len);
    }
    push_zeros(out, snapshot.team_id_len);
    push_u32(out, snapshot.security_round);
    push_u32(out, snapshot.last_event_seq);
    push_u32(out, snapshot.last_update_s);
    push_string(out, snapshot.team_name, kTeamNameMaxLen);

    size_t member_count = snapshot.members.size();
    if (member_count > 255)
    {
        member_count = 255;
    }
    push_u8(out, static_cast<uint8_t>(member_count));

    for (size_t i = 0; i < member_count; ++i)
    {
        const TeamStateMemberSnapshot& member = snapshot.members[i];
        push_u32(out, member.node_id);
        push_u8(out, member.leader ? 1 : 0);
        push_u8(out, member.online ? 1 : 0);
        push_u32(out, member.last_seen_s);
        push_string(out, member.name, kMemberNameMaxLen);
    }

    return true;
}

uint32_t hash_bytes(const uint8_t* data, size_t len)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; ++i)
    {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

} // namespace hostlink

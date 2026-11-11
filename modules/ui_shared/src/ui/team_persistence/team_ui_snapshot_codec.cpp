#include "ui/team_persistence/team_ui_snapshot_codec.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace ui::team_persistence
{
namespace
{

constexpr uint8_t kRoleNone = 0;
constexpr uint8_t kRoleMember = 1;
constexpr uint8_t kRoleLeader = 2;
constexpr std::size_t kMaxMemberNameBytes = 24;

uint64_t teamIdToU64(const ::team::TeamId& id)
{
    uint64_t value = 0;
    for (std::size_t i = 0; i < id.size(); ++i)
    {
        value |= static_cast<uint64_t>(id[i]) << (8 * i);
    }
    return value;
}

::team::TeamId teamIdFromU64(uint64_t value)
{
    ::team::TeamId id{};
    for (std::size_t i = 0; i < id.size(); ++i)
    {
        id[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
    }
    return id;
}

void appendU8(std::vector<uint8_t>& out, uint8_t value)
{
    out.push_back(value);
}

void appendU16(std::vector<uint8_t>& out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void appendU32(std::vector<uint8_t>& out, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
    {
        out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
    }
}

void appendU64(std::vector<uint8_t>& out, uint64_t value)
{
    for (int i = 0; i < 8; ++i)
    {
        out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
    }
}

bool readU8(const uint8_t* data, std::size_t len, std::size_t& off, uint8_t& out)
{
    if (off + 1U > len)
    {
        return false;
    }
    out = data[off++];
    return true;
}

bool readU16(const uint8_t* data, std::size_t len, std::size_t& off, uint16_t& out)
{
    if (off + 2U > len)
    {
        return false;
    }
    out = static_cast<uint16_t>(data[off]) |
          (static_cast<uint16_t>(data[off + 1U]) << 8);
    off += 2U;
    return true;
}

bool readU32(const uint8_t* data, std::size_t len, std::size_t& off, uint32_t& out)
{
    if (off + 4U > len)
    {
        return false;
    }
    out = static_cast<uint32_t>(data[off]) |
          (static_cast<uint32_t>(data[off + 1U]) << 8) |
          (static_cast<uint32_t>(data[off + 2U]) << 16) |
          (static_cast<uint32_t>(data[off + 3U]) << 24);
    off += 4U;
    return true;
}

bool readU64(const uint8_t* data, std::size_t len, std::size_t& off, uint64_t& out)
{
    if (off + 8U > len)
    {
        return false;
    }
    out = 0;
    for (int i = 0; i < 8; ++i)
    {
        out |= static_cast<uint64_t>(data[off + static_cast<std::size_t>(i)])
               << (8 * i);
    }
    off += 8U;
    return true;
}

} // namespace

bool encodeTeamUiSnapshot(const ::team::ui::TeamUiSnapshot& snapshot,
                          uint32_t updated_s,
                          std::vector<uint8_t>& out)
{
    out.clear();
    out.reserve(64U + snapshot.members.size() * 36U);

    out.push_back('T');
    out.push_back('M');
    out.push_back('S');
    out.push_back('1');
    appendU8(out, kTeamUiSnapshotCurrentVersion);

    const uint8_t flags = snapshot.in_team ? 0x01 : 0x00;
    appendU8(out, flags);
    appendU16(out, 0);
    appendU32(out, updated_s);
    appendU64(out, snapshot.has_team_id ? teamIdToU64(snapshot.team_id) : 0);
    appendU32(out, snapshot.security_round);
    appendU32(out, snapshot.last_event_seq);
    appendU32(out, 0);

    uint32_t leader_node_id = 0;
    for (const auto& member : snapshot.members)
    {
        if (member.leader)
        {
            leader_node_id = member.node_id;
            break;
        }
    }
    appendU32(out, leader_node_id);
    appendU8(out, snapshot.self_is_leader ? kRoleLeader
                                          : (snapshot.in_team ? kRoleMember
                                                              : kRoleNone));
    appendU8(out, 0);
    appendU8(out, 0);
    appendU8(out, 0);

    const std::size_t capped_count =
        std::min<std::size_t>(snapshot.members.size(), UINT16_MAX);
    appendU16(out, static_cast<uint16_t>(capped_count));
    appendU16(out, 0);

    for (std::size_t i = 0; i < capped_count; ++i)
    {
        const auto& member = snapshot.members[i];
        appendU32(out, member.node_id);
        appendU8(out, member.leader ? kRoleLeader : kRoleMember);

        std::string name = member.name;
        if (name.size() > kMaxMemberNameBytes)
        {
            name.resize(kMaxMemberNameBytes);
        }
        const uint8_t name_flag = name.empty() ? 0x00 : 0x01;
        appendU8(out, name_flag);
        appendU32(out, member.last_seen_s);
        appendU16(out, static_cast<uint16_t>(name.size()));
        out.insert(out.end(), name.begin(), name.end());
    }

    appendU32(out, snapshot.team_chat_unread);
    return true;
}

bool decodeTeamUiSnapshot(const uint8_t* data,
                          std::size_t len,
                          ::team::ui::TeamUiSnapshot& out)
{
    if (data == nullptr || len < 4U || std::memcmp(data, "TMS1", 4) != 0)
    {
        return false;
    }

    std::size_t off = 4U;
    uint8_t version = 0;
    uint8_t flags = 0;
    uint16_t reserved = 0;
    uint32_t updated_ts = 0;
    uint64_t team_id = 0;
    uint32_t epoch = 0;
    uint32_t last_event_seq = 0;
    uint32_t self_node_id = 0;
    uint32_t leader_node_id = 0;
    uint8_t self_role = 0;
    uint16_t member_count = 0;
    uint16_t reserved3 = 0;

    if (!readU8(data, len, off, version) ||
        !readU8(data, len, off, flags) ||
        !readU16(data, len, off, reserved) ||
        !readU32(data, len, off, updated_ts) ||
        !readU64(data, len, off, team_id) ||
        !readU32(data, len, off, epoch) ||
        !readU32(data, len, off, last_event_seq) ||
        !readU32(data, len, off, self_node_id) ||
        !readU32(data, len, off, leader_node_id) ||
        !readU8(data, len, off, self_role))
    {
        return false;
    }
    if (off + 3U > len)
    {
        return false;
    }
    off += 3U;
    if (!readU16(data, len, off, member_count) ||
        !readU16(data, len, off, reserved3))
    {
        return false;
    }

    if (version != kTeamUiSnapshotVersionV1 &&
        version != kTeamUiSnapshotVersionV2)
    {
        return false;
    }

    ::team::ui::TeamUiSnapshot decoded;
    decoded.in_team = (flags & 0x01) != 0;
    decoded.has_team_id = (team_id != 0);
    decoded.team_id = teamIdFromU64(team_id);
    decoded.security_round = epoch;
    decoded.last_event_seq = last_event_seq;
    decoded.last_update_s = updated_ts;
    decoded.self_is_leader = (self_role == kRoleLeader);

    for (uint16_t i = 0; i < member_count; ++i)
    {
        uint32_t node_id = 0;
        uint8_t role = 0;
        uint8_t member_flags = 0;
        uint32_t last_seen_s = 0;
        uint16_t name_len = 0;

        if (!readU32(data, len, off, node_id) ||
            !readU8(data, len, off, role) ||
            !readU8(data, len, off, member_flags))
        {
            return false;
        }
        if (version >= kTeamUiSnapshotVersionV2 &&
            !readU32(data, len, off, last_seen_s))
        {
            return false;
        }
        if (!readU16(data, len, off, name_len))
        {
            return false;
        }
        if (off + name_len > len)
        {
            return false;
        }

        std::string name;
        if (name_len > 0)
        {
            name.assign(reinterpret_cast<const char*>(data + off), name_len);
        }
        off += name_len;

        ::team::ui::TeamMemberUi member;
        member.node_id = node_id;
        member.name = name;
        member.leader = (role == kRoleLeader);
        member.last_seen_s = last_seen_s;
        decoded.members.push_back(member);
    }

    uint32_t chat_unread = 0;
    if (off + sizeof(chat_unread) <= len && readU32(data, len, off, chat_unread))
    {
        decoded.team_chat_unread = chat_unread;
    }

    (void)reserved;
    (void)reserved3;
    (void)self_node_id;
    (void)leader_node_id;
    out = decoded;
    return true;
}

} // namespace ui::team_persistence

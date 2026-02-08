#include "team_mgmt.h"

#include <cstring>

namespace team::proto
{
namespace
{

struct ByteWriter
{
    explicit ByteWriter(std::vector<uint8_t>& out_ref) : out(out_ref) {}

    void putU8(uint8_t v) { out.push_back(v); }
    void putU16(uint16_t v)
    {
        out.push_back(static_cast<uint8_t>(v & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    }
    void putU32(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
        {
            out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
        }
    }
    void putU64(uint64_t v)
    {
        for (int i = 0; i < 8; ++i)
        {
            out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
        }
    }
    void putBytes(const uint8_t* data, size_t len)
    {
        out.insert(out.end(), data, data + len);
    }

    std::vector<uint8_t>& out;
};

struct ByteReader
{
    ByteReader(const uint8_t* data_ref, size_t len_ref) : data(data_ref), len(len_ref) {}

    bool getU8(uint8_t* out_val)
    {
        if (!ensure(1)) return false;
        *out_val = data[pos++];
        return true;
    }

    bool getU16(uint16_t* out_val)
    {
        if (!ensure(2)) return false;
        *out_val = static_cast<uint16_t>(data[pos]) |
                   static_cast<uint16_t>(data[pos + 1] << 8);
        pos += 2;
        return true;
    }

    bool getU32(uint32_t* out_val)
    {
        if (!ensure(4)) return false;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
        {
            v |= static_cast<uint32_t>(data[pos + i]) << (8 * i);
        }
        *out_val = v;
        pos += 4;
        return true;
    }

    bool getU64(uint64_t* out_val)
    {
        if (!ensure(8)) return false;
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
        {
            v |= static_cast<uint64_t>(data[pos + i]) << (8 * i);
        }
        *out_val = v;
        pos += 8;
        return true;
    }

    bool getBytes(uint8_t* out_buf, size_t out_len)
    {
        if (!ensure(out_len)) return false;
        memcpy(out_buf, data + pos, out_len);
        pos += out_len;
        return true;
    }

    bool ensure(size_t count) const { return (pos + count) <= len; }

    const uint8_t* data;
    size_t len;
    size_t pos = 0;
};

bool encodeTeamParams(const TeamParams& params, ByteWriter& writer)
{
    if (!params.has_params)
    {
        return true;
    }
    writer.putU32(params.position_interval_ms);
    writer.putU8(params.precision_level);
    writer.putU32(params.flags);
    return true;
}

bool decodeTeamParams(ByteReader& reader, TeamParams* params)
{
    if (!params)
    {
        return false;
    }
    params->has_params = true;
    if (!reader.getU32(&params->position_interval_ms)) return false;
    if (!reader.getU8(&params->precision_level)) return false;
    if (!reader.getU32(&params->flags)) return false;
    return true;
}

} // namespace

bool encodeTeamMgmtMessage(TeamMgmtType type,
                           const std::vector<uint8_t>& payload,
                           std::vector<uint8_t>& out)
{
    if (payload.size() > 0xFFFF)
    {
        return false;
    }
    out.clear();
    ByteWriter writer(out);
    writer.putU8(kTeamMgmtVersion);
    writer.putU8(static_cast<uint8_t>(type));
    writer.putU16(0); // reserved
    writer.putU16(static_cast<uint16_t>(payload.size()));
    if (!payload.empty())
    {
        writer.putBytes(payload.data(), payload.size());
    }
    return true;
}

bool decodeTeamMgmtMessage(const uint8_t* data, size_t len,
                           uint8_t* out_version,
                           TeamMgmtType* out_type,
                           std::vector<uint8_t>& out_payload)
{
    if (!data || !out_version || !out_type)
    {
        return false;
    }
    ByteReader reader(data, len);
    uint16_t reserved = 0;
    uint16_t payload_len = 0;
    uint8_t type_raw = 0;

    if (!reader.getU8(out_version)) return false;
    if (!reader.getU8(&type_raw)) return false;
    if (!reader.getU16(&reserved)) return false;
    if (!reader.getU16(&payload_len)) return false;
    if (!reader.ensure(payload_len)) return false;

    out_payload.assign(data + reader.pos, data + reader.pos + payload_len);
    *out_type = static_cast<TeamMgmtType>(type_raw);
    return true;
}

bool encodeTeamKick(const TeamKick& input, std::vector<uint8_t>& out)
{
    out.clear();
    ByteWriter writer(out);
    writer.putU32(input.target);
    return true;
}

bool decodeTeamKick(const uint8_t* data, size_t len, TeamKick* out)
{
    if (!data || !out)
    {
        return false;
    }
    ByteReader reader(data, len);
    if (!reader.getU32(&out->target)) return false;
    return true;
}

bool encodeTeamTransferLeader(const TeamTransferLeader& input, std::vector<uint8_t>& out)
{
    out.clear();
    ByteWriter writer(out);
    writer.putU32(input.target);
    return true;
}

bool decodeTeamTransferLeader(const uint8_t* data, size_t len, TeamTransferLeader* out)
{
    if (!data || !out)
    {
        return false;
    }
    ByteReader reader(data, len);
    if (!reader.getU32(&out->target)) return false;
    return true;
}

bool encodeTeamKeyDist(const TeamKeyDist& input, std::vector<uint8_t>& out)
{
    if (input.channel_psk_len > input.channel_psk.size())
    {
        return false;
    }
    out.clear();
    ByteWriter writer(out);
    writer.putBytes(input.team_id.data(), input.team_id.size());
    writer.putU32(input.key_id);
    writer.putU8(input.channel_psk_len);
    writer.putBytes(input.channel_psk.data(), input.channel_psk_len);
    return true;
}

bool decodeTeamKeyDist(const uint8_t* data, size_t len, TeamKeyDist* out)
{
    if (!data || !out)
    {
        return false;
    }
    ByteReader reader(data, len);
    if (!reader.getBytes(out->team_id.data(), out->team_id.size())) return false;
    if (!reader.getU32(&out->key_id)) return false;
    if (!reader.getU8(&out->channel_psk_len)) return false;
    if (out->channel_psk_len > out->channel_psk.size()) return false;
    if (!reader.getBytes(out->channel_psk.data(), out->channel_psk_len)) return false;
    return true;
}

bool encodeTeamStatus(const TeamStatus& input, std::vector<uint8_t>& out)
{
    out.clear();
    ByteWriter writer(out);
    writer.putBytes(input.member_list_hash.data(), input.member_list_hash.size());
    writer.putU32(input.key_id);

    uint16_t flags = 0;
    if (input.params.has_params) flags |= 0x01;
    if (input.has_members) flags |= 0x02;
    writer.putU16(flags);
    if (!encodeTeamParams(input.params, writer)) return false;
    if (input.has_members)
    {
        size_t count = input.members.size();
        if (count > kTeamStatusMaxMembers)
        {
            count = kTeamStatusMaxMembers;
        }
        writer.putU32(input.leader_id);
        writer.putU8(static_cast<uint8_t>(count));
        for (size_t i = 0; i < count; ++i)
        {
            writer.putU32(input.members[i]);
        }
    }
    return true;
}

bool decodeTeamStatus(const uint8_t* data, size_t len, TeamStatus* out)
{
    if (!data || !out)
    {
        return false;
    }
    ByteReader reader(data, len);
    uint16_t flags = 0;
    if (!reader.getBytes(out->member_list_hash.data(), out->member_list_hash.size())) return false;
    if (!reader.getU32(&out->key_id)) return false;
    if (!reader.getU16(&flags)) return false;

    out->params.has_params = (flags & 0x01) != 0;
    if (out->params.has_params && !decodeTeamParams(reader, &out->params)) return false;
    out->has_members = (flags & 0x02) != 0;
    out->members.clear();
    out->leader_id = 0;
    if (out->has_members)
    {
        if (!reader.getU32(&out->leader_id)) return false;
        uint8_t count = 0;
        if (!reader.getU8(&count)) return false;
        out->members.reserve(count);
        for (uint8_t i = 0; i < count; ++i)
        {
            uint32_t id = 0;
            if (!reader.getU32(&id)) return false;
            if (out->members.size() < kTeamStatusMaxMembers)
            {
                out->members.push_back(id);
            }
        }
    }
    return true;
}

} // namespace team::proto

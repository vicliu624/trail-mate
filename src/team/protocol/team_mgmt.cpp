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

bool encodeTeamAdvertise(const TeamAdvertise& input, std::vector<uint8_t>& out)
{
    out.clear();
    ByteWriter writer(out);
    writer.putBytes(input.team_id.data(), input.team_id.size());

    uint16_t flags = 0;
    if (input.has_join_hint) flags |= 0x01;
    if (input.has_channel_index) flags |= 0x02;
    if (input.has_expires_at) flags |= 0x04;
    writer.putU16(flags);

    if (input.has_join_hint) writer.putU32(input.join_hint);
    if (input.has_channel_index) writer.putU8(input.channel_index);
    if (input.has_expires_at) writer.putU64(input.expires_at);
    writer.putU64(input.nonce);
    return true;
}

bool decodeTeamAdvertise(const uint8_t* data, size_t len, TeamAdvertise* out)
{
    if (!data || !out)
    {
        return false;
    }
    ByteReader reader(data, len);
    uint16_t flags = 0;

    if (!reader.getBytes(out->team_id.data(), out->team_id.size())) return false;
    if (!reader.getU16(&flags)) return false;

    out->has_join_hint = (flags & 0x01) != 0;
    out->has_channel_index = (flags & 0x02) != 0;
    out->has_expires_at = (flags & 0x04) != 0;

    if (out->has_join_hint && !reader.getU32(&out->join_hint)) return false;
    if (out->has_channel_index && !reader.getU8(&out->channel_index)) return false;
    if (out->has_expires_at && !reader.getU64(&out->expires_at)) return false;
    if (!reader.getU64(&out->nonce)) return false;
    return true;
}

bool encodeTeamJoinRequest(const TeamJoinRequest& input, std::vector<uint8_t>& out)
{
    if (input.has_member_pub && input.member_pub_len > input.member_pub.size())
    {
        return false;
    }
    out.clear();
    ByteWriter writer(out);
    writer.putBytes(input.team_id.data(), input.team_id.size());

    uint16_t flags = 0;
    if (input.has_member_pub) flags |= 0x01;
    if (input.has_capabilities) flags |= 0x02;
    writer.putU16(flags);

    if (input.has_member_pub)
    {
        writer.putU8(input.member_pub_len);
        writer.putBytes(input.member_pub.data(), input.member_pub_len);
    }
    if (input.has_capabilities) writer.putU32(input.capabilities);
    writer.putU64(input.nonce);
    return true;
}

bool decodeTeamJoinRequest(const uint8_t* data, size_t len, TeamJoinRequest* out)
{
    if (!data || !out)
    {
        return false;
    }
    ByteReader reader(data, len);
    uint16_t flags = 0;
    if (!reader.getBytes(out->team_id.data(), out->team_id.size())) return false;
    if (!reader.getU16(&flags)) return false;

    out->has_member_pub = (flags & 0x01) != 0;
    out->has_capabilities = (flags & 0x02) != 0;

    if (out->has_member_pub)
    {
        uint8_t pub_len = 0;
        if (!reader.getU8(&pub_len)) return false;
        if (pub_len > out->member_pub.size()) return false;
        out->member_pub_len = pub_len;
        if (!reader.getBytes(out->member_pub.data(), pub_len)) return false;
    }
    if (out->has_capabilities && !reader.getU32(&out->capabilities)) return false;
    if (!reader.getU64(&out->nonce)) return false;
    return true;
}

bool encodeTeamJoinAccept(const TeamJoinAccept& input, std::vector<uint8_t>& out)
{
    if (input.channel_psk_len > input.channel_psk.size())
    {
        return false;
    }
    out.clear();
    ByteWriter writer(out);
    writer.putU8(input.channel_index);
    writer.putU8(input.channel_psk_len);
    writer.putBytes(input.channel_psk.data(), input.channel_psk_len);
    writer.putU32(input.key_id);

    uint16_t flags = 0;
    if (input.params.has_params) flags |= 0x01;
    if (input.has_team_id) flags |= 0x02;
    writer.putU16(flags);
    if (!encodeTeamParams(input.params, writer)) return false;
    if (input.has_team_id)
    {
        writer.putBytes(input.team_id.data(), input.team_id.size());
    }

    return true;
}

bool decodeTeamJoinAccept(const uint8_t* data, size_t len, TeamJoinAccept* out)
{
    if (!data || !out)
    {
        return false;
    }
    ByteReader reader(data, len);
    uint16_t flags = 0;

    if (!reader.getU8(&out->channel_index)) return false;
    if (!reader.getU8(&out->channel_psk_len)) return false;
    if (out->channel_psk_len > out->channel_psk.size()) return false;
    if (!reader.getBytes(out->channel_psk.data(), out->channel_psk_len)) return false;
    if (!reader.getU32(&out->key_id)) return false;
    if (!reader.getU16(&flags)) return false;

    out->params.has_params = (flags & 0x01) != 0;
    out->has_team_id = (flags & 0x02) != 0;
    if (out->params.has_params && !decodeTeamParams(reader, &out->params)) return false;
    if (out->has_team_id && !reader.getBytes(out->team_id.data(), out->team_id.size())) return false;
    return true;
}

bool encodeTeamJoinConfirm(const TeamJoinConfirm& input, std::vector<uint8_t>& out)
{
    out.clear();
    ByteWriter writer(out);
    writer.putU8(input.ok ? 1 : 0);

    uint16_t flags = 0;
    if (input.has_capabilities) flags |= 0x01;
    if (input.has_battery) flags |= 0x02;
    writer.putU16(flags);

    if (input.has_capabilities) writer.putU32(input.capabilities);
    if (input.has_battery) writer.putU8(input.battery);
    return true;
}

bool decodeTeamJoinConfirm(const uint8_t* data, size_t len, TeamJoinConfirm* out)
{
    if (!data || !out)
    {
        return false;
    }
    ByteReader reader(data, len);
    uint16_t flags = 0;
    uint8_t ok = 0;
    if (!reader.getU8(&ok)) return false;
    out->ok = (ok != 0);
    if (!reader.getU16(&flags)) return false;

    out->has_capabilities = (flags & 0x01) != 0;
    out->has_battery = (flags & 0x02) != 0;

    if (out->has_capabilities && !reader.getU32(&out->capabilities)) return false;
    if (out->has_battery && !reader.getU8(&out->battery)) return false;
    return true;
}

bool encodeTeamJoinDecision(const TeamJoinDecision& input, std::vector<uint8_t>& out)
{
    out.clear();
    ByteWriter writer(out);
    writer.putU8(input.accept ? 1 : 0);
    uint16_t flags = 0;
    if (input.has_reason) flags |= 0x01;
    writer.putU16(flags);
    if (input.has_reason) writer.putU32(input.reason);
    return true;
}

bool decodeTeamJoinDecision(const uint8_t* data, size_t len, TeamJoinDecision* out)
{
    if (!data || !out)
    {
        return false;
    }
    ByteReader reader(data, len);
    uint16_t flags = 0;
    uint8_t accept = 0;
    if (!reader.getU8(&accept)) return false;
    out->accept = (accept != 0);
    if (!reader.getU16(&flags)) return false;
    out->has_reason = (flags & 0x01) != 0;
    if (out->has_reason && !reader.getU32(&out->reason)) return false;
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
    writer.putU16(flags);
    if (!encodeTeamParams(input.params, writer)) return false;
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
    return true;
}

} // namespace team::proto

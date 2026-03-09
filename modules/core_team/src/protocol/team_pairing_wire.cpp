#include "team/protocol/team_pairing_wire.h"

#include <cstring>

namespace team::proto::pairing
{
namespace
{
constexpr uint8_t kPairingMagic0 = 'T';
constexpr uint8_t kPairingMagic1 = 'M';
constexpr size_t kHeaderSize = 4;

struct ByteWriter
{
    explicit ByteWriter(std::vector<uint8_t>& out_ref) : out(out_ref) {}

    void putU8(uint8_t v) { out.push_back(v); }
    void putU32(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
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

void writeHeader(MessageType type, ByteWriter& writer)
{
    writer.putU8(kPairingMagic0);
    writer.putU8(kPairingMagic1);
    writer.putU8(kPairingVersion);
    writer.putU8(static_cast<uint8_t>(type));
}

bool readHeader(ByteReader& reader, MessageType* out_type)
{
    uint8_t magic0 = 0;
    uint8_t magic1 = 0;
    uint8_t version = 0;
    uint8_t type_raw = 0;
    if (!reader.getU8(&magic0)) return false;
    if (!reader.getU8(&magic1)) return false;
    if (!reader.getU8(&version)) return false;
    if (!reader.getU8(&type_raw)) return false;
    if (magic0 != kPairingMagic0 || magic1 != kPairingMagic1) return false;
    if (version != kPairingVersion) return false;
    if (out_type)
    {
        *out_type = static_cast<MessageType>(type_raw);
    }
    return true;
}

} // namespace

bool decodeType(const uint8_t* data, size_t len, MessageType* out_type)
{
    if (!data || !out_type || len < kHeaderSize)
    {
        return false;
    }
    ByteReader reader(data, len);
    return readHeader(reader, out_type);
}

bool encodeBeacon(const BeaconPacket& input, std::vector<uint8_t>& out)
{
    out.clear();
    ByteWriter writer(out);
    writeHeader(MessageType::Beacon, writer);
    writer.putBytes(input.team_id.data(), input.team_id.size());
    writer.putU32(input.key_id);
    writer.putU32(input.leader_id);
    writer.putU32(input.window_ms);
    uint8_t name_len = 0;
    if (input.has_team_name)
    {
        name_len = static_cast<uint8_t>(strnlen(input.team_name, kMaxTeamNameLen));
    }
    writer.putU8(name_len);
    if (name_len > 0)
    {
        writer.putBytes(reinterpret_cast<const uint8_t*>(input.team_name), name_len);
    }
    return true;
}

bool decodeBeacon(const uint8_t* data, size_t len, BeaconPacket* out)
{
    if (!data || !out)
    {
        return false;
    }
    ByteReader reader(data, len);
    MessageType type = MessageType::Beacon;
    if (!readHeader(reader, &type) || type != MessageType::Beacon) return false;
    if (!reader.getBytes(out->team_id.data(), out->team_id.size())) return false;
    if (!reader.getU32(&out->key_id)) return false;
    if (!reader.getU32(&out->leader_id)) return false;
    if (!reader.getU32(&out->window_ms)) return false;
    uint8_t name_len = 0;
    if (!reader.getU8(&name_len)) return false;
    if (name_len > kMaxTeamNameLen) return false;
    memset(out->team_name, 0, sizeof(out->team_name));
    out->has_team_name = (name_len > 0);
    if (name_len > 0)
    {
        if (!reader.getBytes(reinterpret_cast<uint8_t*>(out->team_name), name_len)) return false;
        out->team_name[name_len] = '\0';
    }
    return true;
}

bool encodeJoin(const JoinPacket& input, std::vector<uint8_t>& out)
{
    out.clear();
    ByteWriter writer(out);
    writeHeader(MessageType::Join, writer);
    writer.putBytes(input.team_id.data(), input.team_id.size());
    writer.putU32(input.member_id);
    writer.putU32(input.nonce);
    return true;
}

bool decodeJoin(const uint8_t* data, size_t len, JoinPacket* out)
{
    if (!data || !out)
    {
        return false;
    }
    ByteReader reader(data, len);
    MessageType type = MessageType::Beacon;
    if (!readHeader(reader, &type) || type != MessageType::Join) return false;
    if (!reader.getBytes(out->team_id.data(), out->team_id.size())) return false;
    if (!reader.getU32(&out->member_id)) return false;
    if (!reader.getU32(&out->nonce)) return false;
    return true;
}

bool encodeKey(const KeyPacket& input, std::vector<uint8_t>& out)
{
    if (input.channel_psk_len > input.channel_psk.size())
    {
        return false;
    }
    out.clear();
    ByteWriter writer(out);
    writeHeader(MessageType::Key, writer);
    writer.putBytes(input.team_id.data(), input.team_id.size());
    writer.putU32(input.key_id);
    writer.putU32(input.nonce);
    writer.putU8(input.channel_psk_len);
    if (input.channel_psk_len > 0)
    {
        writer.putBytes(input.channel_psk.data(), input.channel_psk_len);
    }
    return true;
}

bool decodeKey(const uint8_t* data, size_t len, KeyPacket* out)
{
    if (!data || !out)
    {
        return false;
    }
    ByteReader reader(data, len);
    MessageType type = MessageType::Beacon;
    if (!readHeader(reader, &type) || type != MessageType::Key) return false;
    if (!reader.getBytes(out->team_id.data(), out->team_id.size())) return false;
    if (!reader.getU32(&out->key_id)) return false;
    if (!reader.getU32(&out->nonce)) return false;
    if (!reader.getU8(&out->channel_psk_len)) return false;
    if (out->channel_psk_len > out->channel_psk.size()) return false;
    out->channel_psk.fill(0);
    if (out->channel_psk_len > 0)
    {
        if (!reader.getBytes(out->channel_psk.data(), out->channel_psk_len)) return false;
    }
    return true;
}

} // namespace team::proto::pairing

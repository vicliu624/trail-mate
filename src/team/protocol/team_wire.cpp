#include "team_wire.h"

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

    bool getBytes(uint8_t* out_buf, size_t out_len)
    {
        if (!ensure(out_len)) return false;
        memcpy(out_buf, data + pos, out_len);
        pos += out_len;
        return true;
    }

    bool skip(size_t count)
    {
        if (!ensure(count)) return false;
        pos += count;
        return true;
    }

    bool ensure(size_t count) const { return (pos + count) <= len; }

    const uint8_t* data;
    size_t len;
    size_t pos = 0;
};

} // namespace

bool encodeTeamEncrypted(const TeamEncrypted& input, std::vector<uint8_t>& out)
{
    if (input.ciphertext.size() > 0xFFFF)
    {
        return false;
    }

    out.clear();
    ByteWriter writer(out);
    writer.putU8(input.version);
    writer.putU8(input.aad_flags);
    writer.putU16(0); // reserved
    writer.putU32(input.key_id);
    writer.putBytes(input.team_id.data(), input.team_id.size());
    writer.putBytes(input.nonce.data(), input.nonce.size());
    writer.putU16(static_cast<uint16_t>(input.ciphertext.size()));
    if (!input.ciphertext.empty())
    {
        writer.putBytes(input.ciphertext.data(), input.ciphertext.size());
    }

    return true;
}

bool decodeTeamEncrypted(const uint8_t* data, size_t len, TeamEncrypted* out)
{
    if (!data || !out)
    {
        return false;
    }

    ByteReader reader(data, len);
    uint16_t reserved = 0;
    uint16_t cipher_len = 0;

    if (!reader.getU8(&out->version)) return false;
    if (!reader.getU8(&out->aad_flags)) return false;
    if (!reader.getU16(&reserved)) return false;
    if (!reader.getU32(&out->key_id)) return false;
    if (!reader.getBytes(out->team_id.data(), out->team_id.size())) return false;
    if (!reader.getBytes(out->nonce.data(), out->nonce.size())) return false;
    if (!reader.getU16(&cipher_len)) return false;
    if (!reader.ensure(cipher_len)) return false;

    out->ciphertext.assign(data + reader.pos, data + reader.pos + cipher_len);
    return true;
}

} // namespace team::proto

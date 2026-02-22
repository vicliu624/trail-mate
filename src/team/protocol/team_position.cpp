/**
 * @file team_position.cpp
 * @brief Team position protocol encoding/decoding
 */

#include "team_position.h"

namespace team::proto
{
namespace
{

void write_u16_le(std::vector<uint8_t>& out, uint16_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void write_u32_le(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void write_i16_le(std::vector<uint8_t>& out, int16_t v)
{
    write_u16_le(out, static_cast<uint16_t>(v));
}

void write_i32_le(std::vector<uint8_t>& out, int32_t v)
{
    write_u32_le(out, static_cast<uint32_t>(v));
}

bool read_u8(const uint8_t* data, size_t len, size_t& off, uint8_t& out)
{
    if (off + 1 > len)
    {
        return false;
    }
    out = data[off++];
    return true;
}

bool read_u16_le(const uint8_t* data, size_t len, size_t& off, uint16_t& out)
{
    if (off + 2 > len)
    {
        return false;
    }
    out = static_cast<uint16_t>(data[off]) |
          (static_cast<uint16_t>(data[off + 1]) << 8);
    off += 2;
    return true;
}

bool read_u32_le(const uint8_t* data, size_t len, size_t& off, uint32_t& out)
{
    if (off + 4 > len)
    {
        return false;
    }
    out = static_cast<uint32_t>(data[off]) |
          (static_cast<uint32_t>(data[off + 1]) << 8) |
          (static_cast<uint32_t>(data[off + 2]) << 16) |
          (static_cast<uint32_t>(data[off + 3]) << 24);
    off += 4;
    return true;
}

bool read_i16_le(const uint8_t* data, size_t len, size_t& off, int16_t& out)
{
    uint16_t tmp = 0;
    if (!read_u16_le(data, len, off, tmp))
    {
        return false;
    }
    out = static_cast<int16_t>(tmp);
    return true;
}

bool read_i32_le(const uint8_t* data, size_t len, size_t& off, int32_t& out)
{
    uint32_t tmp = 0;
    if (!read_u32_le(data, len, off, tmp))
    {
        return false;
    }
    out = static_cast<int32_t>(tmp);
    return true;
}

} // namespace

bool encodeTeamPositionMessage(const TeamPositionMessage& msg, std::vector<uint8_t>& out)
{
    out.clear();
    out.reserve(1 + 2 + 4 + 4 + 2 + 2 + 2 + 1 + 4);
    out.push_back(msg.version);
    write_u16_le(out, msg.flags);
    write_i32_le(out, msg.lat_e7);
    write_i32_le(out, msg.lon_e7);
    write_i16_le(out, msg.alt_m);
    write_u16_le(out, msg.speed_dmps);
    write_u16_le(out, msg.course_cdeg);
    out.push_back(msg.sats_in_view);
    write_u32_le(out, msg.ts);
    return true;
}

bool decodeTeamPositionMessage(const uint8_t* data, size_t len, TeamPositionMessage* out)
{
    if (!data || !out)
    {
        return false;
    }
    if (len < (1 + 2 + 4 + 4 + 2 + 2 + 2 + 1 + 4))
    {
        return false;
    }

    size_t off = 0;
    TeamPositionMessage msg{};
    if (!read_u8(data, len, off, msg.version))
    {
        return false;
    }
    if (msg.version != kTeamPositionVersion)
    {
        return false;
    }
    if (!read_u16_le(data, len, off, msg.flags))
    {
        return false;
    }
    if (!read_i32_le(data, len, off, msg.lat_e7))
    {
        return false;
    }
    if (!read_i32_le(data, len, off, msg.lon_e7))
    {
        return false;
    }
    if (!read_i16_le(data, len, off, msg.alt_m))
    {
        return false;
    }
    if (!read_u16_le(data, len, off, msg.speed_dmps))
    {
        return false;
    }
    if (!read_u16_le(data, len, off, msg.course_cdeg))
    {
        return false;
    }
    if (!read_u8(data, len, off, msg.sats_in_view))
    {
        return false;
    }
    if (!read_u32_le(data, len, off, msg.ts))
    {
        return false;
    }

    *out = msg;
    return true;
}

} // namespace team::proto

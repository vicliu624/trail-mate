/**
 * @file team_track.cpp
 * @brief Team track protocol encoding/decoding
 */

#include "team_track.h"

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

void write_i32_le(std::vector<uint8_t>& out, int32_t v)
{
    write_u32_le(out, static_cast<uint32_t>(v));
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

bool encodeTeamTrackMessage(const TeamTrackMessage& msg, std::vector<uint8_t>& out)
{
    if (msg.points.size() > kTeamTrackMaxPoints)
    {
        return false;
    }
    const uint8_t count = static_cast<uint8_t>(msg.points.size());
    uint32_t valid_mask = msg.valid_mask;
    if (valid_mask == 0 && count > 0)
    {
        valid_mask = (count >= 32) ? 0xFFFFFFFFu : ((1u << count) - 1u);
    }
    out.clear();
    out.reserve(1 + 4 + 2 + 1 + 4 + (static_cast<size_t>(count) * 8));
    out.push_back(msg.version);
    write_u32_le(out, msg.start_ts);
    write_u16_le(out, msg.interval_s);
    out.push_back(count);
    write_u32_le(out, valid_mask);
    for (size_t i = 0; i < count; ++i)
    {
        write_i32_le(out, msg.points[i].lat_e7);
        write_i32_le(out, msg.points[i].lon_e7);
    }
    return true;
}

bool decodeTeamTrackMessage(const uint8_t* data, size_t len, TeamTrackMessage* out)
{
    if (!data || !out || len < 1 + 4 + 2 + 1)
    {
        return false;
    }
    size_t off = 0;
    TeamTrackMessage msg{};
    msg.version = data[off++];
    if (msg.version != kTeamTrackVersion)
    {
        return false;
    }
    if (!read_u32_le(data, len, off, msg.start_ts))
    {
        return false;
    }
    if (!read_u16_le(data, len, off, msg.interval_s))
    {
        return false;
    }
    const uint8_t count = data[off++];
    if (count > kTeamTrackMaxPoints)
    {
        return false;
    }
    const size_t remaining = (len > off) ? (len - off) : 0;
    const size_t points_bytes = static_cast<size_t>(count) * 8;
    if (remaining >= (4 + points_bytes))
    {
        if (!read_u32_le(data, len, off, msg.valid_mask))
        {
            return false;
        }
    }
    else
    {
        msg.valid_mask = (count == 0) ? 0u : ((1u << count) - 1u);
    }
    const size_t needed = off + (static_cast<size_t>(count) * 8);
    if (len < needed)
    {
        return false;
    }
    msg.points.clear();
    msg.points.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        TeamTrackPoint pt{};
        if (!read_i32_le(data, len, off, pt.lat_e7))
        {
            return false;
        }
        if (!read_i32_le(data, len, off, pt.lon_e7))
        {
            return false;
        }
        msg.points.push_back(pt);
    }
    *out = std::move(msg);
    return true;
}

} // namespace team::proto

/**
 * @file team_waypoint.cpp
 * @brief Team waypoint protocol encoding/decoding
 */

#include "team_waypoint.h"

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

bool read_string(const uint8_t* data, size_t len, size_t& off,
                 size_t max_len, std::string& out)
{
    uint16_t str_len = 0;
    if (!read_u16_le(data, len, off, str_len))
    {
        return false;
    }
    if (str_len > max_len || off + str_len > len)
    {
        return false;
    }
    out.assign(reinterpret_cast<const char*>(data + off), str_len);
    off += str_len;
    return true;
}

} // namespace

bool encodeTeamWaypointMessage(const TeamWaypointMessage& msg, std::vector<uint8_t>& out)
{
    if (msg.name.size() > kTeamWaypointNameMaxLen ||
        msg.description.size() > kTeamWaypointDescMaxLen ||
        msg.icon.size() > kTeamWaypointIconMaxLen)
    {
        return false;
    }

    out.clear();
    out.reserve(1 + 2 + 4 + 4 + 4 + 4 + 4 + 2 + msg.name.size() + 2 + msg.description.size() +
                2 + msg.icon.size());
    out.push_back(msg.version);
    write_u16_le(out, msg.flags);
    write_u32_le(out, msg.id);
    write_i32_le(out, msg.lat_e7);
    write_i32_le(out, msg.lon_e7);
    write_u32_le(out, msg.expire_ts);
    write_u32_le(out, msg.locked_to);
    write_u16_le(out, static_cast<uint16_t>(msg.name.size()));
    if (!msg.name.empty())
    {
        out.insert(out.end(), msg.name.begin(), msg.name.end());
    }
    write_u16_le(out, static_cast<uint16_t>(msg.description.size()));
    if (!msg.description.empty())
    {
        out.insert(out.end(), msg.description.begin(), msg.description.end());
    }
    write_u16_le(out, static_cast<uint16_t>(msg.icon.size()));
    if (!msg.icon.empty())
    {
        out.insert(out.end(), msg.icon.begin(), msg.icon.end());
    }
    return true;
}

bool decodeTeamWaypointMessage(const uint8_t* data, size_t len, TeamWaypointMessage* out)
{
    if (!data || !out)
    {
        return false;
    }
    const size_t kMinLen = 1 + 2 + 4 + 4 + 4 + 4 + 4 + 2 + 2 + 2;
    if (len < kMinLen)
    {
        return false;
    }

    size_t off = 0;
    TeamWaypointMessage msg{};
    msg.version = data[off++];
    if (msg.version != kTeamWaypointVersion)
    {
        return false;
    }
    if (!read_u16_le(data, len, off, msg.flags))
    {
        return false;
    }
    if (!read_u32_le(data, len, off, msg.id))
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
    if (!read_u32_le(data, len, off, msg.expire_ts))
    {
        return false;
    }
    if (!read_u32_le(data, len, off, msg.locked_to))
    {
        return false;
    }
    if (!read_string(data, len, off, kTeamWaypointNameMaxLen, msg.name))
    {
        return false;
    }
    if (!read_string(data, len, off, kTeamWaypointDescMaxLen, msg.description))
    {
        return false;
    }
    if (!read_string(data, len, off, kTeamWaypointIconMaxLen, msg.icon))
    {
        return false;
    }

    if (off != len)
    {
        return false;
    }
    *out = std::move(msg);
    return true;
}

} // namespace team::proto

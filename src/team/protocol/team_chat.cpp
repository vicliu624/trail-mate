/**
 * @file team_chat.cpp
 * @brief Team chat protocol encoding/decoding
 */

#include "team_chat.h"

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

void write_i16_le(std::vector<uint8_t>& out, int16_t v)
{
    write_u16_le(out, static_cast<uint16_t>(v));
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
} // namespace

bool encodeTeamChatMessage(const TeamChatMessage& msg, std::vector<uint8_t>& out)
{
    out.clear();
    out.reserve(1 + 1 + 2 + 4 + 4 + 4 + msg.payload.size());
    out.push_back(msg.header.version);
    out.push_back(static_cast<uint8_t>(msg.header.type));
    write_u16_le(out, msg.header.flags);
    write_u32_le(out, msg.header.msg_id);
    write_u32_le(out, msg.header.ts);
    write_u32_le(out, msg.header.from);
    out.insert(out.end(), msg.payload.begin(), msg.payload.end());
    return true;
}

bool decodeTeamChatMessage(const uint8_t* data, size_t len, TeamChatMessage* out)
{
    if (!data || !out || len < 1 + 1 + 2 + 4 + 4 + 4)
    {
        return false;
    }
    size_t off = 0;
    TeamChatHeader header{};
    header.version = data[off++];
    header.type = static_cast<TeamChatType>(data[off++]);
    if (!read_u16_le(data, len, off, header.flags))
    {
        return false;
    }
    if (!read_u32_le(data, len, off, header.msg_id))
    {
        return false;
    }
    if (!read_u32_le(data, len, off, header.ts))
    {
        return false;
    }
    if (!read_u32_le(data, len, off, header.from))
    {
        return false;
    }
    out->header = header;
    out->payload.assign(data + off, data + len);
    return true;
}

bool encodeTeamChatLocation(const TeamChatLocation& loc, std::vector<uint8_t>& out)
{
    out.clear();
    out.reserve(4 + 4 + 2 + 2 + 4 + 1 + 2 + loc.label.size());
    write_i32_le(out, loc.lat_e7);
    write_i32_le(out, loc.lon_e7);
    write_i16_le(out, loc.alt_m);
    write_u16_le(out, loc.acc_m);
    write_u32_le(out, loc.ts);
    out.push_back(loc.source);
    uint16_t label_len = static_cast<uint16_t>(loc.label.size());
    write_u16_le(out, label_len);
    if (label_len > 0)
    {
        out.insert(out.end(), loc.label.begin(), loc.label.end());
    }
    return true;
}

bool decodeTeamChatLocation(const uint8_t* data, size_t len, TeamChatLocation* out)
{
    if (!data || !out)
    {
        return false;
    }
    size_t off = 0;
    if (!read_i32_le(data, len, off, out->lat_e7))
    {
        return false;
    }
    if (!read_i32_le(data, len, off, out->lon_e7))
    {
        return false;
    }
    if (!read_i16_le(data, len, off, out->alt_m))
    {
        return false;
    }
    if (!read_u16_le(data, len, off, out->acc_m))
    {
        return false;
    }
    if (!read_u32_le(data, len, off, out->ts))
    {
        return false;
    }
    if (off + 1 > len)
    {
        return false;
    }
    out->source = data[off++];
    uint16_t label_len = 0;
    if (!read_u16_le(data, len, off, label_len))
    {
        return false;
    }
    if (off + label_len > len)
    {
        return false;
    }
    out->label.assign(reinterpret_cast<const char*>(data + off), label_len);
    return true;
}

bool encodeTeamChatCommand(const TeamChatCommand& cmd, std::vector<uint8_t>& out)
{
    out.clear();
    out.reserve(1 + 4 + 4 + 2 + 1 + 2 + cmd.note.size());
    out.push_back(static_cast<uint8_t>(cmd.cmd_type));
    write_i32_le(out, cmd.lat_e7);
    write_i32_le(out, cmd.lon_e7);
    write_u16_le(out, cmd.radius_m);
    out.push_back(cmd.priority);
    uint16_t note_len = static_cast<uint16_t>(cmd.note.size());
    write_u16_le(out, note_len);
    if (note_len > 0)
    {
        out.insert(out.end(), cmd.note.begin(), cmd.note.end());
    }
    return true;
}

bool decodeTeamChatCommand(const uint8_t* data, size_t len, TeamChatCommand* out)
{
    if (!data || !out || len < 1)
    {
        return false;
    }
    size_t off = 0;
    out->cmd_type = static_cast<TeamCommandType>(data[off++]);
    if (!read_i32_le(data, len, off, out->lat_e7))
    {
        return false;
    }
    if (!read_i32_le(data, len, off, out->lon_e7))
    {
        return false;
    }
    if (!read_u16_le(data, len, off, out->radius_m))
    {
        return false;
    }
    if (off + 1 > len)
    {
        return false;
    }
    out->priority = data[off++];
    uint16_t note_len = 0;
    if (!read_u16_le(data, len, off, note_len))
    {
        return false;
    }
    if (off + note_len > len)
    {
        return false;
    }
    out->note.assign(reinterpret_cast<const char*>(data + off), note_len);
    return true;
}

} // namespace team::proto

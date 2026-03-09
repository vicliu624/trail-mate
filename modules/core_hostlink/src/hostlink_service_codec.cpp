#include "hostlink/hostlink_service_codec.h"

#include <cmath>
#include <cstring>

namespace hostlink
{

namespace
{

void push_u16(std::vector<uint8_t>& out, uint16_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void push_u32(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void push_i32(std::vector<uint8_t>& out, int32_t v)
{
    push_u32(out, static_cast<uint32_t>(v));
}

bool parse_u16_le(const uint8_t* data, size_t len, size_t& off, uint16_t& out)
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

bool parse_u32_le(const uint8_t* data, size_t len, size_t& off, uint32_t& out)
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

bool try_parse_tx_app_data_legacy(const Frame& frame, uint8_t max_channels, PendingCommand& out)
{
    size_t off = 0;
    uint32_t portnum = 0;
    uint32_t to = 0;
    uint16_t payload_len = 0;
    if (!parse_u32_le(frame.payload.data(), frame.payload.size(), off, portnum) ||
        !parse_u32_le(frame.payload.data(), frame.payload.size(), off, to))
    {
        return false;
    }
    if (off + 2 > frame.payload.size())
    {
        return false;
    }
    const uint8_t channel = frame.payload[off++];
    const uint8_t flags = frame.payload[off++];
    if (!parse_u16_le(frame.payload.data(), frame.payload.size(), off, payload_len))
    {
        return false;
    }
    if (off + payload_len != frame.payload.size())
    {
        return false;
    }
    if (channel >= max_channels || payload_len > kMaxFrameLen)
    {
        return false;
    }

    out.type = PendingCommandType::TxAppData;
    out.to = to;
    out.portnum = portnum;
    out.channel = channel;
    out.flags = flags;
    out.payload_len = payload_len;
    if (payload_len > 0)
    {
        std::memcpy(out.payload, frame.payload.data() + off, payload_len);
    }
    return true;
}

bool try_parse_tx_app_data_extended(const Frame& frame,
                                    uint8_t max_channels,
                                    size_t team_id_size,
                                    bool include_timestamp_field,
                                    PendingCommand& out)
{
    size_t off = 0;
    uint32_t portnum = 0;
    uint32_t from = 0;
    uint32_t to = 0;
    uint32_t team_key_id = 0;
    uint32_t timestamp_s = 0;
    uint32_t total_len = 0;
    uint32_t chunk_offset = 0;
    uint16_t chunk_len = 0;
    if (!parse_u32_le(frame.payload.data(), frame.payload.size(), off, portnum) ||
        !parse_u32_le(frame.payload.data(), frame.payload.size(), off, from) ||
        !parse_u32_le(frame.payload.data(), frame.payload.size(), off, to))
    {
        return false;
    }
    if (off + 2 > frame.payload.size())
    {
        return false;
    }
    const uint8_t channel = frame.payload[off++];
    const uint8_t flags = frame.payload[off++];
    if (off + team_id_size > frame.payload.size())
    {
        return false;
    }
    off += team_id_size;
    if (!parse_u32_le(frame.payload.data(), frame.payload.size(), off, team_key_id))
    {
        return false;
    }
    if (include_timestamp_field &&
        !parse_u32_le(frame.payload.data(), frame.payload.size(), off, timestamp_s))
    {
        return false;
    }
    if (!parse_u32_le(frame.payload.data(), frame.payload.size(), off, total_len) ||
        !parse_u32_le(frame.payload.data(), frame.payload.size(), off, chunk_offset) ||
        !parse_u16_le(frame.payload.data(), frame.payload.size(), off, chunk_len))
    {
        return false;
    }
    if (off + chunk_len != frame.payload.size())
    {
        return false;
    }
    if (channel >= max_channels || chunk_len > kMaxFrameLen)
    {
        return false;
    }
    if (chunk_offset != 0 || total_len != chunk_len)
    {
        return false;
    }

    (void)from;
    (void)team_key_id;
    (void)timestamp_s;

    out.type = PendingCommandType::TxAppData;
    out.to = to;
    out.portnum = portnum;
    out.channel = channel;
    out.flags = flags;
    out.payload_len = chunk_len;
    if (chunk_len > 0)
    {
        std::memcpy(out.payload, frame.payload.data() + off, chunk_len);
    }
    return true;
}

} // namespace

bool build_hello_ack_payload(std::vector<uint8_t>& out, const HelloAckPayloadInfo& info)
{
    if (!info.model || !info.firmware)
    {
        return false;
    }

    out.clear();
    out.reserve(32);
    out.push_back(static_cast<uint8_t>(info.protocol_version & 0xFF));
    out.push_back(static_cast<uint8_t>((info.protocol_version >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(info.max_frame_len & 0xFF));
    out.push_back(static_cast<uint8_t>((info.max_frame_len >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(info.capabilities & 0xFF));
    out.push_back(static_cast<uint8_t>((info.capabilities >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((info.capabilities >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((info.capabilities >> 24) & 0xFF));

    const size_t model_len = std::strlen(info.model);
    const size_t firmware_len = std::strlen(info.firmware);
    if (model_len > 255 || firmware_len > 255)
    {
        return false;
    }

    out.push_back(static_cast<uint8_t>(model_len));
    out.insert(out.end(), info.model, info.model + model_len);
    out.push_back(static_cast<uint8_t>(firmware_len));
    out.insert(out.end(), info.firmware, info.firmware + firmware_len);
    return true;
}

bool build_gps_payload(std::vector<uint8_t>& out, const GpsPayloadSnapshot& snapshot)
{
    out.clear();
    out.reserve(24);

    uint8_t flags = 0;
    if (snapshot.valid)
    {
        flags |= 0x01;
    }
    if (snapshot.has_alt)
    {
        flags |= 0x02;
    }
    if (snapshot.has_speed)
    {
        flags |= 0x04;
    }
    if (snapshot.has_course)
    {
        flags |= 0x08;
    }

    out.push_back(flags);
    out.push_back(snapshot.satellites);
    push_u32(out, snapshot.age);

    int32_t lat_e7 = 0;
    int32_t lon_e7 = 0;
    if (snapshot.valid)
    {
        lat_e7 = static_cast<int32_t>(std::lround(snapshot.lat * 10000000.0));
        lon_e7 = static_cast<int32_t>(std::lround(snapshot.lng * 10000000.0));
    }
    push_i32(out, lat_e7);
    push_i32(out, lon_e7);

    int32_t alt_cm = 0;
    if (snapshot.has_alt)
    {
        alt_cm = static_cast<int32_t>(std::lround(snapshot.alt_m * 100.0));
    }
    push_i32(out, alt_cm);

    uint16_t speed_cms = 0;
    if (snapshot.has_speed)
    {
        double speed = snapshot.speed_mps * 100.0;
        if (speed < 0.0)
        {
            speed = 0.0;
        }
        if (speed > 65535.0)
        {
            speed = 65535.0;
        }
        speed_cms = static_cast<uint16_t>(std::lround(speed));
    }
    push_u16(out, speed_cms);

    uint16_t course_cdeg = 0;
    if (snapshot.has_course)
    {
        double course = snapshot.course_deg * 100.0;
        if (course < 0.0)
        {
            course = 0.0;
        }
        if (course > 35999.0)
        {
            course = 35999.0;
        }
        course_cdeg = static_cast<uint16_t>(std::lround(course));
    }
    push_u16(out, course_cdeg);
    return true;
}

bool parse_u64_le(const uint8_t* data, size_t len, size_t& off, uint64_t& out)
{
    if (off + 8 > len)
    {
        return false;
    }
    out = 0;
    for (int i = 0; i < 8; ++i)
    {
        out |= static_cast<uint64_t>(data[off + i]) << (8 * i);
    }
    off += 8;
    return true;
}

bool parse_tx_msg_command(const Frame& frame, uint8_t max_channels, PendingCommand& out)
{
    size_t off = 0;
    uint32_t to = 0;
    uint16_t text_len = 0;
    if (!parse_u32_le(frame.payload.data(), frame.payload.size(), off, to))
    {
        return false;
    }
    if (off + 2 > frame.payload.size())
    {
        return false;
    }

    const uint8_t channel = frame.payload[off++];
    const uint8_t flags = frame.payload[off++];
    if (!parse_u16_le(frame.payload.data(), frame.payload.size(), off, text_len))
    {
        return false;
    }
    if (off + text_len > frame.payload.size() || channel >= max_channels)
    {
        return false;
    }

    out = PendingCommand{};
    out.type = PendingCommandType::TxMsg;
    out.to = to;
    out.channel = channel;
    out.flags = flags;
    out.payload_len = text_len;
    if (text_len > 0)
    {
        std::memcpy(out.payload, frame.payload.data() + off, text_len);
    }
    return true;
}

bool parse_tx_app_data_command(const Frame& frame,
                               uint8_t max_channels,
                               size_t team_id_size,
                               PendingCommand& out)
{
    out = PendingCommand{};
    return try_parse_tx_app_data_legacy(frame, max_channels, out) ||
           try_parse_tx_app_data_extended(frame, max_channels, team_id_size, false, out) ||
           try_parse_tx_app_data_extended(frame, max_channels, team_id_size, true, out);
}

} // namespace hostlink

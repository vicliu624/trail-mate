#include "hostlink/hostlink_app_data_codec.h"

#include <cstring>
#include <limits>

namespace hostlink
{

namespace
{

void push_u8(std::vector<uint8_t>& out, uint8_t v)
{
    out.push_back(v);
}

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

void push_tlv(std::vector<uint8_t>& out, uint8_t key, const void* data, size_t len)
{
    if (!data || len == 0 || len > 255)
    {
        return;
    }
    out.push_back(key);
    out.push_back(static_cast<uint8_t>(len));
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + len);
}

void push_tlv_u8(std::vector<uint8_t>& out, uint8_t key, uint8_t value)
{
    push_tlv(out, key, &value, sizeof(value));
}

void push_tlv_u32(std::vector<uint8_t>& out, uint8_t key, uint32_t value)
{
    uint8_t buf[4] = {static_cast<uint8_t>(value & 0xFF),
                      static_cast<uint8_t>((value >> 8) & 0xFF),
                      static_cast<uint8_t>((value >> 16) & 0xFF),
                      static_cast<uint8_t>((value >> 24) & 0xFF)};
    push_tlv(out, key, buf, sizeof(buf));
}

void push_tlv_i16(std::vector<uint8_t>& out, uint8_t key, int16_t value)
{
    uint8_t buf[2] = {static_cast<uint8_t>(value & 0xFF),
                      static_cast<uint8_t>((value >> 8) & 0xFF)};
    push_tlv(out, key, buf, sizeof(buf));
}

void push_bytes(std::vector<uint8_t>& out, const uint8_t* data, size_t len)
{
    if (!data || len == 0)
    {
        return;
    }
    out.insert(out.end(), data, data + len);
}

void push_zeros(std::vector<uint8_t>& out, size_t len)
{
    out.insert(out.end(), len, 0);
}

size_t app_data_header_size(size_t team_id_size)
{
    return 4 + 4 + 4 + 1 + 1 + team_id_size + 4 + 4 + 4 + 4 + 2;
}

void append_app_data_header(std::vector<uint8_t>& out,
                            const AppDataFrameEncodeRequest& request,
                            size_t team_id_size,
                            uint8_t flags,
                            uint32_t total_len,
                            uint32_t offset,
                            uint16_t chunk_len)
{
    push_u32(out, request.portnum);
    push_u32(out, request.from);
    push_u32(out, request.to);
    push_u8(out, request.channel);
    push_u8(out, flags);
    if (request.team_id)
    {
        push_bytes(out, request.team_id, team_id_size);
    }
    else
    {
        push_zeros(out, team_id_size);
    }
    push_u32(out, request.team_key_id);
    push_u32(out, request.timestamp_s);
    push_u32(out, total_len);
    push_u32(out, offset);
    push_u16(out, chunk_len);
}

} // namespace

void build_rx_meta_tlvs(const chat::RxMeta& meta, uint32_t packet_id, std::vector<uint8_t>& out)
{
    out.clear();
    if (meta.rx_timestamp_s != 0)
    {
        push_tlv_u32(out, static_cast<uint8_t>(AppDataMetaKey::RxTimestampS),
                     meta.rx_timestamp_s);
    }
    if (meta.rx_timestamp_ms != 0)
    {
        push_tlv_u32(out, static_cast<uint8_t>(AppDataMetaKey::RxTimestampMs),
                     meta.rx_timestamp_ms);
    }
    if (meta.time_source != chat::RxTimeSource::Unknown)
    {
        push_tlv_u8(out, static_cast<uint8_t>(AppDataMetaKey::RxTimeSource),
                    static_cast<uint8_t>(meta.time_source));
    }
    if (meta.hop_count != 0xFF)
    {
        push_tlv_u8(out, static_cast<uint8_t>(AppDataMetaKey::HopCount), meta.hop_count);
        push_tlv_u8(out, static_cast<uint8_t>(AppDataMetaKey::Direct), meta.direct ? 1 : 0);
    }
    if (meta.hop_limit != 0xFF)
    {
        push_tlv_u8(out, static_cast<uint8_t>(AppDataMetaKey::HopLimit), meta.hop_limit);
    }
    if (meta.origin != chat::RxOrigin::Unknown)
    {
        push_tlv_u8(out, static_cast<uint8_t>(AppDataMetaKey::RxOrigin),
                    static_cast<uint8_t>(meta.origin));
        push_tlv_u8(out, static_cast<uint8_t>(AppDataMetaKey::FromIs), meta.from_is ? 1 : 0);
    }
    if (meta.channel_hash != 0xFF)
    {
        push_tlv_u8(out, static_cast<uint8_t>(AppDataMetaKey::ChannelHash), meta.channel_hash);
    }
    if (meta.wire_flags != 0xFF)
    {
        push_tlv_u8(out, static_cast<uint8_t>(AppDataMetaKey::WireFlags), meta.wire_flags);
    }
    if (meta.next_hop != 0)
    {
        push_tlv_u32(out, static_cast<uint8_t>(AppDataMetaKey::NextHop), meta.next_hop);
    }
    if (meta.relay_node != 0)
    {
        push_tlv_u32(out, static_cast<uint8_t>(AppDataMetaKey::RelayNode), meta.relay_node);
    }
    if (meta.rssi_dbm_x10 != std::numeric_limits<int16_t>::min())
    {
        push_tlv_i16(out, static_cast<uint8_t>(AppDataMetaKey::RssiDbmX10), meta.rssi_dbm_x10);
    }
    if (meta.snr_db_x10 != std::numeric_limits<int16_t>::min())
    {
        push_tlv_i16(out, static_cast<uint8_t>(AppDataMetaKey::SnrDbX10), meta.snr_db_x10);
    }
    if (meta.freq_hz != 0)
    {
        push_tlv_u32(out, static_cast<uint8_t>(AppDataMetaKey::FreqHz), meta.freq_hz);
    }
    if (meta.bw_hz != 0)
    {
        push_tlv_u32(out, static_cast<uint8_t>(AppDataMetaKey::BwHz), meta.bw_hz);
    }
    if (meta.sf != 0)
    {
        push_tlv_u8(out, static_cast<uint8_t>(AppDataMetaKey::Sf), meta.sf);
    }
    if (meta.cr != 0)
    {
        push_tlv_u8(out, static_cast<uint8_t>(AppDataMetaKey::Cr), meta.cr);
    }
    if (packet_id != 0)
    {
        push_tlv_u32(out, static_cast<uint8_t>(AppDataMetaKey::PacketId), packet_id);
    }
}

bool encode_app_data_frames(const AppDataFrameEncodeRequest& request,
                            size_t max_frame_len,
                            size_t team_id_size,
                            std::vector<std::vector<uint8_t>>& out_frames)
{
    out_frames.clear();
    if (request.payload_len > 0 && request.payload == nullptr)
    {
        return false;
    }
    if (request.payload_len > static_cast<size_t>(std::numeric_limits<uint32_t>::max()))
    {
        return false;
    }

    std::vector<uint8_t> meta_tlv;
    if (request.rx_meta)
    {
        build_rx_meta_tlvs(*request.rx_meta, request.packet_id, meta_tlv);
    }

    const size_t header_size = app_data_header_size(team_id_size);
    if (max_frame_len <= header_size + meta_tlv.size())
    {
        return false;
    }

    const size_t max_chunk = max_frame_len - header_size - meta_tlv.size();
    if (request.payload_len > 0 && max_chunk == 0)
    {
        return false;
    }

    if (request.payload_len == 0)
    {
        std::vector<uint8_t> frame;
        frame.reserve(header_size + meta_tlv.size());
        append_app_data_header(frame, request, team_id_size, request.flags, 0, 0, 0);
        if (!meta_tlv.empty())
        {
            push_bytes(frame, meta_tlv.data(), meta_tlv.size());
        }
        out_frames.push_back(frame);
        return true;
    }

    size_t offset = 0;
    while (offset < request.payload_len)
    {
        size_t chunk_len = request.payload_len - offset;
        if (chunk_len > max_chunk)
        {
            chunk_len = max_chunk;
        }

        uint8_t chunk_flags = request.flags;
        if (offset + chunk_len < request.payload_len)
        {
            chunk_flags |= kAppDataFlagMoreChunks;
        }

        std::vector<uint8_t> frame;
        frame.reserve(header_size + chunk_len + meta_tlv.size());
        append_app_data_header(frame,
                               request,
                               team_id_size,
                               chunk_flags,
                               static_cast<uint32_t>(request.payload_len),
                               static_cast<uint32_t>(offset),
                               static_cast<uint16_t>(chunk_len));
        push_bytes(frame, request.payload + offset, chunk_len);
        if (!meta_tlv.empty())
        {
            push_bytes(frame, meta_tlv.data(), meta_tlv.size());
        }
        out_frames.push_back(frame);
        offset += chunk_len;
    }

    return !out_frames.empty();
}

} // namespace hostlink

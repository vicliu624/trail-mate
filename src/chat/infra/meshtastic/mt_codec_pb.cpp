/**
 * @file mt_codec_pb.cpp
 * @brief Meshtastic protocol codec implementation using protobuf
 *
 * Based on M5Tab5-GPS implementation
 */

#include "mt_codec_pb.h"
#include "compression/unishox2.h"
#include <cstring>

namespace chat
{
namespace meshtastic
{

bool encodeTextMessage(ChannelId channel, const std::string& text,
                       NodeId from_node, uint32_t packet_id,
                       uint8_t* out_buffer, size_t* out_size)
{
    if (!out_buffer || !out_size || text.empty())
    {
        return false;
    }

    // Create Data message (like M5Tab5-GPS make_packet_payload)
    meshtastic_Data data = meshtastic_Data_init_default;
    data.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    data.want_response = false;
    data.has_bitfield = true;
    data.bitfield = 0; // No special flags for now

    // Set text payload
    size_t text_len = text.length();
    if (text_len > sizeof(data.payload.bytes))
    {
        return false; // Text too long
    }
    data.payload.size = text_len;
    memcpy(data.payload.bytes, text.c_str(), text_len);

    // Encode Data message
    uint8_t data_buf[256];
    pb_ostream_t data_stream = pb_ostream_from_buffer(data_buf, sizeof(data_buf));
    if (!pb_encode(&data_stream, meshtastic_Data_fields, &data))
    {
        return false;
    }
    size_t data_len = data_stream.bytes_written;

    // Return encoded Data payload
    // The full packet with wire header will be constructed in mt_adapter
    if (*out_size < data_len)
    {
        *out_size = data_len;
        return false;
    }

    memcpy(out_buffer, data_buf, data_len);
    *out_size = data_len;
    return true;
}

bool decodeTextMessage(const uint8_t* buffer, size_t size, MeshIncomingText* out)
{
    if (!buffer || !out || size == 0)
    {
        return false;
    }

    // Decode Data message (like M5Tab5-GPS)
    meshtastic_Data data = meshtastic_Data_init_default;
    pb_istream_t stream = pb_istream_from_buffer(buffer, size);
    if (!pb_decode(&stream, meshtastic_Data_fields, &data))
    {
        return false;
    }

    // Check port number
    if (data.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP &&
        data.portnum != meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP)
    {
        return false;
    }

    // Extract text (decompress if needed)
    if (data.payload.size == 0 || data.payload.size > sizeof(data.payload.bytes))
    {
        return false;
    }

    if (data.portnum == meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP)
    {
        char decompressed[256];
        memset(decompressed, 0, sizeof(decompressed));
        int out_len = unishox2_decompress_simple(
            reinterpret_cast<const char*>(data.payload.bytes),
            static_cast<int>(data.payload.size),
            decompressed);
        if (out_len <= 0)
        {
            return false;
        }
        out->text.assign(decompressed, static_cast<size_t>(out_len));
    }
    else
    {
        out->text.assign(reinterpret_cast<const char*>(data.payload.bytes), data.payload.size);
    }
    // Note: from, msg_id, timestamp, channel should be extracted from packet header
    // This will be done in mt_adapter when decoding full packet
    out->from = 0;   // Will be set from packet header
    out->msg_id = 0; // Will be set from packet header
    out->timestamp = millis() / 1000;
    out->channel = ChannelId::PRIMARY; // Will be set from packet header
    out->hop_limit = 2;
    out->encrypted = false;

    return true;
}

bool encodeNodeInfoMessage(const std::string& user_id, const std::string& long_name,
                           const std::string& short_name, meshtastic_HardwareModel hw_model,
                           const uint8_t macaddr[6], const uint8_t* public_key, size_t public_key_len,
                           bool want_response, uint8_t* out_buffer, size_t* out_size)
{
    if (!out_buffer || !out_size)
    {
        return false;
    }

    meshtastic_User user = meshtastic_User_init_default;
    memset(user.id, 0, sizeof(user.id));
    memset(user.long_name, 0, sizeof(user.long_name));
    memset(user.short_name, 0, sizeof(user.short_name));

    strncpy(user.id, user_id.c_str(), sizeof(user.id) - 1);
    strncpy(user.long_name, long_name.c_str(), sizeof(user.long_name) - 1);
    strncpy(user.short_name, short_name.c_str(), sizeof(user.short_name) - 1);

    if (macaddr != nullptr)
    {
        memcpy(user.macaddr, macaddr, sizeof(user.macaddr));
    }
    if (public_key != nullptr && public_key_len == 32)
    {
        user.public_key.size = static_cast<pb_size_t>(public_key_len);
        memcpy(user.public_key.bytes, public_key, public_key_len);
    }
    user.hw_model = hw_model;
    user.role = meshtastic_Config_DeviceConfig_Role_CLIENT;

    uint8_t user_buf[128];
    pb_ostream_t user_stream = pb_ostream_from_buffer(user_buf, sizeof(user_buf));
    if (!pb_encode(&user_stream, meshtastic_User_fields, &user))
    {
        return false;
    }
    size_t user_len = user_stream.bytes_written;

    meshtastic_Data data = meshtastic_Data_init_default;
    data.portnum = meshtastic_PortNum_NODEINFO_APP;
    data.want_response = want_response;
    data.has_bitfield = true;
    data.bitfield = 0;

    if (user_len > sizeof(data.payload.bytes))
    {
        return false;
    }
    data.payload.size = user_len;
    memcpy(data.payload.bytes, user_buf, user_len);

    pb_ostream_t data_stream = pb_ostream_from_buffer(out_buffer, *out_size);
    if (!pb_encode(&data_stream, meshtastic_Data_fields, &data))
    {
        return false;
    }

    *out_size = data_stream.bytes_written;
    return true;
}

bool encodeAppData(uint32_t portnum, const uint8_t* payload, size_t payload_len,
                   bool want_response, uint8_t* out_buffer, size_t* out_size)
{
    if (!out_buffer || !out_size)
    {
        return false;
    }

    meshtastic_Data data = meshtastic_Data_init_default;
    data.portnum = static_cast<meshtastic_PortNum>(portnum);
    data.want_response = want_response;
    data.has_bitfield = true;
    data.bitfield = 0;

    if (payload_len > sizeof(data.payload.bytes))
    {
        return false;
    }
    data.payload.size = static_cast<pb_size_t>(payload_len);
    if (payload_len > 0)
    {
        if (!payload)
        {
            return false;
        }
        memcpy(data.payload.bytes, payload, payload_len);
    }

    pb_ostream_t data_stream = pb_ostream_from_buffer(out_buffer, *out_size);
    if (!pb_encode(&data_stream, meshtastic_Data_fields, &data))
    {
        return false;
    }

    *out_size = data_stream.bytes_written;
    return true;
}

bool encodeMeshPacket(const meshtastic_MeshPacket& packet, uint8_t* out_buffer, size_t* out_size)
{
    if (!out_buffer || !out_size)
    {
        return false;
    }

    pb_ostream_t stream = pb_ostream_from_buffer(out_buffer, *out_size);
    if (!pb_encode(&stream, meshtastic_MeshPacket_fields, &packet))
    {
        *out_size = stream.bytes_written;
        return false;
    }

    *out_size = stream.bytes_written;
    return true;
}

bool decodeMeshPacket(const uint8_t* buffer, size_t size, meshtastic_MeshPacket* out)
{
    if (!buffer || !out || size == 0)
    {
        return false;
    }

    pb_istream_t stream = pb_istream_from_buffer(buffer, size);
    return pb_decode(&stream, meshtastic_MeshPacket_fields, out);
}

} // namespace meshtastic
} // namespace chat

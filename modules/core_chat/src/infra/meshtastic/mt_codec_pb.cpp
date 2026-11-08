/**
 * @file mt_codec_pb.cpp
 * @brief Meshtastic protocol codec implementation using protobuf
 *
 * Based on this project's Meshtastic compatibility implementation
 */

#include "chat/infra/meshtastic/mt_codec_pb.h"
#include "chat/infra/mesh_incoming_queue.h"
#include "chat/infra/meshtastic/compression/unishox2.h"
#include "chat/time_utils.h"
#include <algorithm>
#include <cstring>
#include <memory>

namespace chat
{
namespace meshtastic
{

namespace
{

constexpr uint32_t kBitfieldWantResponseMask = 0x02;

meshtastic_Data* acquireDataScratch(meshtastic_Data* supplied,
                                    std::unique_ptr<meshtastic_Data>& owned)
{
    if (supplied != nullptr)
    {
        *supplied = meshtastic_Data_init_default;
        return supplied;
    }
    owned = std::make_unique<meshtastic_Data>();
    *owned = meshtastic_Data_init_default;
    return owned.get();
}

} // namespace

bool encodeTextMessage(ChannelId channel, const std::string& text,
                       NodeId from_node, uint32_t packet_id, NodeId dest_node,
                       uint8_t* out_buffer, size_t* out_size,
                       meshtastic_Data* data_scratch)
{
    return encodeTextMessageBytes(channel,
                                  text.data(),
                                  text.size(),
                                  from_node,
                                  packet_id,
                                  dest_node,
                                  out_buffer,
                                  out_size,
                                  data_scratch);
}

bool encodeTextMessageBytes(ChannelId channel, const char* text, size_t text_len,
                            NodeId from_node, uint32_t packet_id, NodeId dest_node,
                            uint8_t* out_buffer, size_t* out_size,
                            meshtastic_Data* data_scratch)
{
    if (!out_buffer || !out_size || !text || text_len == 0)
    {
        return false;
    }
    (void)channel;

    // Create a Meshtastic Data message payload
    std::unique_ptr<meshtastic_Data> owned_data;
    meshtastic_Data* data = acquireDataScratch(data_scratch, owned_data);
    data->portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    data->want_response = false;
    data->has_bitfield = true;
    data->bitfield = 0; // No special flags for now
    data->dest = dest_node;
    data->source = from_node;

    // Set text payload
    if (text_len > sizeof(data->payload.bytes))
    {
        return false; // Text too long
    }
    data->payload.size = text_len;
    memcpy(data->payload.bytes, text, text_len);

    pb_ostream_t data_stream = pb_ostream_from_buffer(out_buffer, *out_size);
    if (!pb_encode(&data_stream, meshtastic_Data_fields, data))
    {
        return false;
    }
    *out_size = data_stream.bytes_written;
    return true;
}

bool decodeTextPayloadToBuffer(const meshtastic_Data& data,
                               char* out_text,
                               size_t out_text_cap,
                               size_t* out_text_len)
{
    if (!out_text || out_text_cap == 0 || !out_text_len)
    {
        return false;
    }
    *out_text_len = 0;

    if (data.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP &&
        data.portnum != meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP)
    {
        return false;
    }

    if (data.payload.size == 0 || data.payload.size > sizeof(data.payload.bytes))
    {
        return false;
    }

    if (data.portnum == meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP)
    {
        const int out_len = unishox2_decompress(
            reinterpret_cast<const char*>(data.payload.bytes),
            static_cast<int>(data.payload.size),
            out_text,
            static_cast<int>(out_text_cap - 1),
            USX_PSET_DFLT);
        if (out_len <= 0)
        {
            return false;
        }
        const size_t safe_len = std::min<size_t>(static_cast<size_t>(out_len), out_text_cap - 1);
        out_text[safe_len] = '\0';
        *out_text_len = safe_len;
    }
    else
    {
        if (data.payload.size > out_text_cap - 1)
        {
            return false;
        }
        std::memcpy(out_text, data.payload.bytes, data.payload.size);
        out_text[data.payload.size] = '\0';
        *out_text_len = data.payload.size;
    }

    return true;
}

bool decodeTextPayload(const meshtastic_Data& data, MeshIncomingText* out)
{
    if (!out)
    {
        return false;
    }

    char text[::chat::infra::kIncomingTextMaxLen + 1] = {};
    size_t text_len = 0;
    if (!decodeTextPayloadToBuffer(data, text, sizeof(text), &text_len))
    {
        return false;
    }
    out->text.assign(text, text_len);

    // Note: from, msg_id, timestamp, channel should be extracted from packet header
    // This will be done in the adapter when decoding the full packet.
    out->from = 0;
    out->msg_id = 0;
    out->timestamp = now_message_timestamp();
    out->channel = ChannelId::PRIMARY;
    out->hop_limit = 2;
    out->encrypted = false;

    return true;
}

bool decodeTextMessage(const uint8_t* buffer, size_t size, MeshIncomingText* out,
                       meshtastic_Data* data_scratch)
{
    if (!buffer || !out || size == 0)
    {
        return false;
    }

    // Decode a Meshtastic Data message payload
    std::unique_ptr<meshtastic_Data> owned_data;
    meshtastic_Data* data = acquireDataScratch(data_scratch, owned_data);
    pb_istream_t stream = pb_istream_from_buffer(buffer, size);
    if (!pb_decode(&stream, meshtastic_Data_fields, data))
    {
        return false;
    }

    return decodeTextPayload(*data, out);
}

bool decodeKeyVerificationMessage(const uint8_t* buffer, size_t size,
                                  meshtastic_KeyVerification* out,
                                  meshtastic_Data* data_scratch)
{
    if (!buffer || !out || size == 0)
    {
        return false;
    }

    std::unique_ptr<meshtastic_Data> owned_data;
    meshtastic_Data* data = acquireDataScratch(data_scratch, owned_data);
    pb_istream_t stream = pb_istream_from_buffer(buffer, size);
    if (!pb_decode(&stream, meshtastic_Data_fields, data))
    {
        return false;
    }

    if (data->portnum != meshtastic_PortNum_KEY_VERIFICATION_APP)
    {
        return false;
    }

    if (data->payload.size == 0 || data->payload.size > sizeof(data->payload.bytes))
    {
        return false;
    }

    pb_istream_t kv_stream = pb_istream_from_buffer(data->payload.bytes, data->payload.size);
    return pb_decode(&kv_stream, meshtastic_KeyVerification_fields, out);
}

bool encodeNodeInfoMessage(const std::string& user_id, const std::string& long_name,
                           const std::string& short_name, meshtastic_HardwareModel hw_model,
                           const uint8_t macaddr[6], const uint8_t* public_key, size_t public_key_len,
                           bool want_response, uint8_t* out_buffer, size_t* out_size,
                           meshtastic_Data* data_scratch)
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

    std::unique_ptr<meshtastic_Data> owned_data;
    meshtastic_Data* data = acquireDataScratch(data_scratch, owned_data);
    data->portnum = meshtastic_PortNum_NODEINFO_APP;
    data->want_response = want_response;
    data->has_bitfield = true;
    data->bitfield = want_response ? kBitfieldWantResponseMask : 0;

    if (user_len > sizeof(data->payload.bytes))
    {
        return false;
    }
    data->payload.size = user_len;
    memcpy(data->payload.bytes, user_buf, user_len);

    pb_ostream_t data_stream = pb_ostream_from_buffer(out_buffer, *out_size);
    if (!pb_encode(&data_stream, meshtastic_Data_fields, data))
    {
        return false;
    }

    *out_size = data_stream.bytes_written;
    return true;
}

bool encodeAppData(uint32_t portnum, const uint8_t* payload, size_t payload_len,
                   bool want_response, uint8_t* out_buffer, size_t* out_size,
                   meshtastic_Data* data_scratch)
{
    return encodeAppDataWithRequestId(
        portnum,
        payload,
        payload_len,
        want_response,
        0,
        out_buffer,
        out_size,
        data_scratch);
}

bool encodeAppDataWithRequestId(uint32_t portnum, const uint8_t* payload, size_t payload_len,
                                bool want_response, uint32_t request_id,
                                uint8_t* out_buffer, size_t* out_size,
                                meshtastic_Data* data_scratch)
{
    if (!out_buffer || !out_size)
    {
        return false;
    }

    std::unique_ptr<meshtastic_Data> owned_data;
    meshtastic_Data* data = acquireDataScratch(data_scratch, owned_data);
    data->portnum = static_cast<meshtastic_PortNum>(portnum);
    data->want_response = want_response;
    data->has_bitfield = true;
    data->bitfield = want_response ? kBitfieldWantResponseMask : 0;
    data->request_id = request_id;

    if (payload_len > sizeof(data->payload.bytes))
    {
        return false;
    }
    data->payload.size = static_cast<pb_size_t>(payload_len);
    if (payload_len > 0)
    {
        if (!payload)
        {
            return false;
        }
        memcpy(data->payload.bytes, payload, payload_len);
    }

    pb_ostream_t data_stream = pb_ostream_from_buffer(out_buffer, *out_size);
    if (!pb_encode(&data_stream, meshtastic_Data_fields, data))
    {
        return false;
    }

    *out_size = data_stream.bytes_written;
    return true;
}

bool decodeAppData(const uint8_t* buffer, size_t size, MeshIncomingData* out,
                   meshtastic_Data* data_scratch)
{
    if (!buffer || !out || size == 0)
    {
        return false;
    }

    std::unique_ptr<meshtastic_Data> owned_data;
    meshtastic_Data* data = acquireDataScratch(data_scratch, owned_data);
    pb_istream_t stream = pb_istream_from_buffer(buffer, size);
    if (!pb_decode(&stream, meshtastic_Data_fields, data))
    {
        return false;
    }

    return decodeAppPayload(*data, out);
}

bool decodeAppPayload(const meshtastic_Data& data, MeshIncomingData* out)
{
    if (!out)
    {
        return false;
    }

    if (data.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP ||
        data.portnum == meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP)
    {
        return false;
    }

    out->portnum = static_cast<uint32_t>(data.portnum);
    out->want_response = data.want_response;
    out->payload.assign(data.payload.bytes, data.payload.bytes + data.payload.size);
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

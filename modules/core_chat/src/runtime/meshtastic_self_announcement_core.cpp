#include "chat/runtime/meshtastic_self_announcement_core.h"

#include "chat/infra/meshtastic/mt_codec_pb.h"
#include "chat/infra/meshtastic/mt_packet_wire.h"
#include "chat/infra/meshtastic/mt_protocol_helpers.h"

#include <cstdio>
#include <string>

namespace chat::runtime
{
namespace
{

std::string buildMeshtasticUserId(NodeId node_id)
{
    char user_id[16] = {};
    std::snprintf(user_id, sizeof(user_id), "!%08X", static_cast<unsigned>(node_id));
    return user_id;
}

const char* resolveChannelName(ChannelId channel)
{
    return (channel == ChannelId::SECONDARY) ? "Secondary" : "Primary";
}

const uint8_t* resolveChannelKey(const MeshConfig& config, ChannelId channel, size_t* out_len)
{
    if (out_len)
    {
        *out_len = 0;
    }

    if (channel == ChannelId::SECONDARY)
    {
        if (!chat::meshtastic::isZeroKey(config.secondary_key, sizeof(config.secondary_key)))
        {
            if (out_len)
            {
                *out_len = sizeof(config.secondary_key);
            }
            return config.secondary_key;
        }
        return nullptr;
    }

    if (!chat::meshtastic::isZeroKey(config.primary_key, sizeof(config.primary_key)))
    {
        if (out_len)
        {
            *out_len = sizeof(config.primary_key);
        }
        return config.primary_key;
    }

    return nullptr;
}

} // namespace

bool MeshtasticSelfAnnouncementCore::buildNodeInfoPacket(const MeshtasticAnnouncementRequest& request,
                                                         MeshtasticAnnouncementPacket* out_packet)
{
    if (!out_packet || request.packet_id == 0 || request.identity.node_id == 0)
    {
        return false;
    }

    *out_packet = MeshtasticAnnouncementPacket{};

    const std::string user_id = buildMeshtasticUserId(request.identity.node_id);
    uint8_t payload[192] = {};
    size_t payload_size = sizeof(payload);

    if (!chat::meshtastic::encodeNodeInfoMessage(user_id,
                                                 request.identity.long_name,
                                                 request.identity.short_name,
                                                 request.hw_model,
                                                 request.mac_addr,
                                                 request.public_key,
                                                 request.public_key_len,
                                                 request.want_response,
                                                 payload,
                                                 &payload_size))
    {
        return false;
    }

    size_t key_len = 0;
    const uint8_t* key = resolveChannelKey(request.mesh_config, request.channel, &key_len);
    out_packet->channel_hash = chat::meshtastic::computeChannelHash(resolveChannelName(request.channel),
                                                                    key,
                                                                    key_len);
    out_packet->wire_size = sizeof(out_packet->wire);
    if (!chat::meshtastic::buildWirePacket(payload,
                                           payload_size,
                                           request.identity.node_id,
                                           request.packet_id,
                                           request.dest_node,
                                           out_packet->channel_hash,
                                           request.hop_limit,
                                           false,
                                           key,
                                           key_len,
                                           out_packet->wire,
                                           &out_packet->wire_size))
    {
        *out_packet = MeshtasticAnnouncementPacket{};
        return false;
    }

    return true;
}

} // namespace chat::runtime

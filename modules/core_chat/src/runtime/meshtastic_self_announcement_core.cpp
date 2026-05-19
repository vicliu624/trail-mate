#include "chat/runtime/meshtastic_self_announcement_core.h"

#include "chat/infra/meshtastic/mt_codec_pb.h"
#include "chat/infra/meshtastic/mt_packet_wire.h"
#include "chat/infra/meshtastic/mt_protocol_helpers.h"
#include "chat/infra/meshtastic/mt_radio_config.h"
#include "chat/infra/meshtastic/mt_region.h"

#include <cstdio>
#include <cstring>
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

constexpr uint8_t kDefaultPskIndex = 1;

const uint8_t* resolveChannelKey(const MeshConfig& config, ChannelId channel, size_t* out_len)
{
    if (out_len)
    {
        *out_len = 0;
    }

    if (channel == ChannelId::SECONDARY)
    {
        const uint8_t key_len = chat::normalizeMeshtasticChannelKeyLen(config.secondary_key,
                                                                       sizeof(config.secondary_key),
                                                                       config.secondary_key_len);
        if (key_len > 0)
        {
            if (out_len)
            {
                *out_len = key_len;
            }
            return config.secondary_key;
        }
        return nullptr;
    }

    const uint8_t key_len = chat::normalizeMeshtasticChannelKeyLen(config.primary_key,
                                                                   sizeof(config.primary_key),
                                                                   config.primary_key_len);
    if (key_len > 0)
    {
        if (out_len)
        {
            *out_len = key_len;
        }
        return config.primary_key;
    }

    static uint8_t default_primary_psk[16] = {};
    size_t expanded_len = 0;
    chat::meshtastic::expandShortPsk(kDefaultPskIndex, default_primary_psk, &expanded_len);
    if (out_len)
    {
        *out_len = expanded_len;
    }
    return expanded_len > 0 ? default_primary_psk : nullptr;
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
    out_packet->channel_hash = chat::meshtastic::computeChannelHash(
        chat::meshtastic::channelName(request.mesh_config,
                                      request.channel),
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

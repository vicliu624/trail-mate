#include "chat/runtime/meshcore_self_announcement_core.h"

#include "chat/infra/meshcore/meshcore_identity_crypto.h"
#include "chat/infra/meshcore/meshcore_payload_helpers.h"

#include <array>
#include <cstring>

namespace chat::runtime
{
namespace
{

constexpr uint8_t kRouteTypeFlood = 0x01;
constexpr uint8_t kRouteTypeDirect = 0x02;
constexpr uint8_t kPayloadTypeAdvert = 0x04;
constexpr uint8_t kAdvertTypeChat = 0x01;
constexpr uint8_t kAdvertTypeRepeater = 0x02;
constexpr uint8_t kAdvertFlagHasLocation = 0x10;
constexpr uint8_t kAdvertFlagHasName = 0x80;

} // namespace

bool MeshCoreSelfAnnouncementCore::buildAdvertPacket(const MeshCoreAnnouncementRequest& request,
                                                     MeshCoreAnnouncementPacket* out_packet)
{
    if (!out_packet || !request.public_key || !request.private_key ||
        request.public_key_len != chat::meshcore::kMeshCorePubKeySize ||
        request.private_key_len != chat::meshcore::kMeshCorePrivKeySize)
    {
        return false;
    }

    *out_packet = MeshCoreAnnouncementPacket{};

    char display_name[32] = {};
    size_t name_len = chat::meshcore::copyPrintableAscii(request.identity.short_name,
                                                         display_name,
                                                         sizeof(display_name));
    if (name_len == 0)
    {
        name_len = chat::meshcore::copyPrintableAscii(request.identity.long_name,
                                                      display_name,
                                                      sizeof(display_name));
    }

    const uint8_t node_type = request.client_repeat ? kAdvertTypeRepeater : kAdvertTypeChat;
    uint8_t app_data[1 + 8 + sizeof(display_name)] = {};
    size_t app_data_len = 0;
    uint8_t flags = static_cast<uint8_t>(node_type & 0x0F);
    if (request.include_location)
    {
        flags = static_cast<uint8_t>(flags | kAdvertFlagHasLocation);
    }
    if (name_len > 0)
    {
        flags = static_cast<uint8_t>(flags | kAdvertFlagHasName);
    }

    app_data[app_data_len++] = flags;
    if (request.include_location)
    {
        std::memcpy(app_data + app_data_len, &request.latitude_i6, sizeof(request.latitude_i6));
        app_data_len += sizeof(request.latitude_i6);
        std::memcpy(app_data + app_data_len, &request.longitude_i6, sizeof(request.longitude_i6));
        app_data_len += sizeof(request.longitude_i6);
    }
    if (name_len > 0)
    {
        std::memcpy(app_data + app_data_len, display_name, name_len);
        app_data_len += name_len;
    }

    std::array<uint8_t, chat::meshcore::kMeshCorePubKeySize + sizeof(uint32_t) + sizeof(app_data)>
        signed_message{};
    size_t signed_len = 0;
    std::memcpy(signed_message.data() + signed_len,
                request.public_key,
                chat::meshcore::kMeshCorePubKeySize);
    signed_len += chat::meshcore::kMeshCorePubKeySize;
    std::memcpy(signed_message.data() + signed_len,
                &request.timestamp_s,
                sizeof(request.timestamp_s));
    signed_len += sizeof(request.timestamp_s);
    if (app_data_len > 0)
    {
        std::memcpy(signed_message.data() + signed_len, app_data, app_data_len);
        signed_len += app_data_len;
    }

    uint8_t signature[chat::meshcore::kMeshCoreSignatureSize] = {};
    if (!chat::meshcore::meshcoreSign(request.private_key,
                                      request.public_key,
                                      signed_message.data(),
                                      signed_len,
                                      signature))
    {
        return false;
    }

    uint8_t payload[184] = {};
    size_t payload_len = 0;
    std::memcpy(payload + payload_len,
                request.public_key,
                chat::meshcore::kMeshCorePubKeySize);
    payload_len += chat::meshcore::kMeshCorePubKeySize;
    std::memcpy(payload + payload_len, &request.timestamp_s, sizeof(request.timestamp_s));
    payload_len += sizeof(request.timestamp_s);
    std::memcpy(payload + payload_len, signature, sizeof(signature));
    payload_len += sizeof(signature);
    if (app_data_len > 0)
    {
        std::memcpy(payload + payload_len, app_data, app_data_len);
        payload_len += app_data_len;
    }

    size_t frame_size = sizeof(out_packet->frame);
    if (!chat::meshcore::buildFrameNoTransport(request.broadcast ? kRouteTypeFlood : kRouteTypeDirect,
                                               kPayloadTypeAdvert,
                                               nullptr,
                                               0,
                                               payload,
                                               payload_len,
                                               out_packet->frame,
                                               frame_size,
                                               &out_packet->frame_size))
    {
        *out_packet = MeshCoreAnnouncementPacket{};
        return false;
    }

    out_packet->node_id = chat::meshcore::deriveNodeIdFromPubkey(request.public_key,
                                                                 request.public_key_len);
    return out_packet->node_id != 0;
}

} // namespace chat::runtime

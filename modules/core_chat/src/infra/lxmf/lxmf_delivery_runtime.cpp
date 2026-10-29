/**
 * @file lxmf_delivery_runtime.cpp
 * @brief Product ingress materialisation for authenticated and source-unknown LXMF deliveries.
 */

#include "chat/infra/lxmf/lxmf_delivery_runtime.h"

#include <cstring>
#include <utility>

namespace chat::lxmf::runtime
{

bool materialiseLxmfTextDelivery(const DecodedTextPayload& payload,
                                 const LxmfDeliveryContext& context,
                                 LxmfMaterialisedText* out_delivery)
{
    if (!out_delivery)
    {
        return false;
    }

    LxmfMaterialisedText delivery{};
    delivery.incoming.channel = ChannelId::PRIMARY;
    delivery.incoming.from = context.peer_node_id;
    delivery.incoming.to = context.destination_is_group ? 0 : context.local_node_id;
    delivery.incoming.msg_id = context.message_id;
    delivery.incoming.timestamp = context.timestamp_s;
    delivery.incoming.hop_limit = 0xFF;
    delivery.incoming.encrypted = context.encrypted;
    delivery.incoming.has_reticulum_lxmf_hash = context.has_message_hash;
    std::memcpy(delivery.incoming.reticulum_lxmf_hash,
                context.message_hash,
                sizeof(delivery.incoming.reticulum_lxmf_hash));
    delivery.incoming.reticulum_identity =
        hasReticulumDestinationIdentity(context.conversation_identity)
            ? context.conversation_identity
            : context.peer_identity;
    delivery.incoming.source_unverified = context.source_unverified;
    delivery.incoming.rx_meta = context.rx_meta;
    delivery.text = payload.content;
    delivery.incoming.text = delivery.text;
    delivery.payload = payload;

    *out_delivery = std::move(delivery);
    return true;
}

bool materialiseLxmfAppDataDelivery(const DecodedAppData& payload,
                                    const LxmfDeliveryContext& context,
                                    LxmfMaterialisedAppData* out_delivery)
{
    if (!out_delivery)
    {
        return false;
    }

    LxmfMaterialisedAppData delivery{};
    delivery.incoming.portnum = payload.portnum;
    delivery.incoming.from = context.peer_node_id;
    delivery.incoming.to = context.local_node_id;
    delivery.incoming.packet_id = payload.packet_id;
    delivery.incoming.request_id = payload.request_id;
    delivery.incoming.channel = ChannelId::PRIMARY;
    delivery.incoming.want_response = payload.want_response;
    delivery.incoming.rx_meta = context.rx_meta;
    delivery.payload.assign(payload.payload.begin(), payload.payload.end());

    *out_delivery = std::move(delivery);
    return true;
}

bool materialiseVerifiedLxmfDelivery(const uint8_t* packed_payload,
                                     std::size_t packed_payload_len,
                                     const LxmfDeliveryContext& context,
                                     LxmfVerifiedDelivery* out_delivery)
{
    if (!out_delivery)
    {
        return false;
    }

    LxmfVerifiedDelivery delivery{};
    if (!packed_payload || packed_payload_len == 0)
    {
        *out_delivery = std::move(delivery);
        return false;
    }

    DecodedAppData app_payload{};
    if (decodeAppDataPayload(packed_payload, packed_payload_len, &app_payload))
    {
        if (!materialiseLxmfAppDataDelivery(app_payload, context, &delivery.app_data))
        {
            *out_delivery = std::move(delivery);
            return false;
        }
        delivery.kind = LxmfDeliveryKind::AppData;
        *out_delivery = std::move(delivery);
        return true;
    }

    DecodedTextPayload text_payload{};
    if (!unpackTextPayload(packed_payload, packed_payload_len, &text_payload))
    {
        *out_delivery = std::move(delivery);
        return false;
    }

    if (!materialiseLxmfTextDelivery(text_payload, context, &delivery.text))
    {
        *out_delivery = std::move(delivery);
        return false;
    }
    delivery.kind = LxmfDeliveryKind::Text;
    *out_delivery = std::move(delivery);
    return true;
}

} // namespace chat::lxmf::runtime

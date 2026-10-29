/**
 * @file lxmf_delivery_runtime.h
 * @brief Product ingress materialisation for authenticated and source-unknown LXMF deliveries.
 */

#pragma once

#include "chat/domain/chat_types.h"
#include "chat/infra/lxmf/lxmf_wire.h"
#include "chat/infra/lxmf/lxmf_memory.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace chat::lxmf::runtime
{

enum class LxmfDeliveryKind : uint8_t
{
    None = 0,
    Text = 1,
    AppData = 2
};

struct LxmfDeliveryContext
{
    NodeId peer_node_id = 0;
    NodeId local_node_id = 0;
    MessageId message_id = 0;
    bool has_message_hash = false;
    uint8_t message_hash[kReticulumLxmfHashSize] = {};
    uint32_t timestamp_s = 0;
    ReticulumPeerIdentity peer_identity{};
    ReticulumPeerIdentity conversation_identity{};
    bool destination_is_group = false;
    bool encrypted = true;
    bool source_unverified = false;
    RxMeta rx_meta{};
};

struct LxmfMaterialisedText
{
    MeshIncomingText incoming{};
    std::string text;
    DecodedTextPayload payload{};
};

struct LxmfMaterialisedAppData
{
    MeshIncomingData incoming{};
    RuntimeByteBuffer payload;
};

struct LxmfVerifiedDelivery
{
    LxmfDeliveryKind kind = LxmfDeliveryKind::None;
    LxmfMaterialisedText text{};
    LxmfMaterialisedAppData app_data{};
};

bool materialiseLxmfTextDelivery(const DecodedTextPayload& payload,
                                 const LxmfDeliveryContext& context,
                                 LxmfMaterialisedText* out_delivery);

bool materialiseLxmfAppDataDelivery(const DecodedAppData& payload,
                                    const LxmfDeliveryContext& context,
                                    LxmfMaterialisedAppData* out_delivery);

bool materialiseVerifiedLxmfDelivery(const uint8_t* packed_payload,
                                     std::size_t packed_payload_len,
                                     const LxmfDeliveryContext& context,
                                     LxmfVerifiedDelivery* out_delivery);

} // namespace chat::lxmf::runtime

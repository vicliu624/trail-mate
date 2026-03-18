#pragma once

#include "chat/domain/chat_types.h"
#include "chat/runtime/self_announcement_core.h"
#include "meshtastic/mesh.pb.h"

#include <cstddef>
#include <cstdint>

namespace chat::runtime
{

struct MeshtasticAnnouncementRequest
{
    EffectiveSelfIdentity identity{};
    MeshConfig mesh_config{};
    ChannelId channel = ChannelId::PRIMARY;
    uint32_t packet_id = 0;
    NodeId dest_node = 0xFFFFFFFFUL;
    uint8_t hop_limit = 2;
    bool want_response = false;
    meshtastic_HardwareModel hw_model = meshtastic_HardwareModel_UNSET;
    const uint8_t* mac_addr = nullptr;
    const uint8_t* public_key = nullptr;
    size_t public_key_len = 0;
};

struct MeshtasticAnnouncementPacket
{
    uint8_t wire[384] = {};
    size_t wire_size = 0;
    uint8_t channel_hash = 0;
};

class MeshtasticSelfAnnouncementCore final : public SelfAnnouncementCore
{
  public:
    static bool buildNodeInfoPacket(const MeshtasticAnnouncementRequest& request,
                                    MeshtasticAnnouncementPacket* out_packet);
};

} // namespace chat::runtime

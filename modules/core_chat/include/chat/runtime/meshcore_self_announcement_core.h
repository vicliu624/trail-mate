#pragma once

#include "chat/domain/chat_types.h"
#include "chat/runtime/self_announcement_core.h"

#include <cstddef>
#include <cstdint>

namespace chat::runtime
{

struct MeshCoreAnnouncementRequest
{
    EffectiveSelfIdentity identity{};
    MeshConfig mesh_config{};
    bool broadcast = true;
    bool include_location = false;
    int32_t latitude_i6 = 0;
    int32_t longitude_i6 = 0;
    uint32_t timestamp_s = 0;
    bool client_repeat = false;
    const uint8_t* public_key = nullptr;
    size_t public_key_len = 0;
    const uint8_t* private_key = nullptr;
    size_t private_key_len = 0;
};

struct MeshCoreAnnouncementPacket
{
    uint8_t frame[255] = {};
    size_t frame_size = 0;
    NodeId node_id = 0;
};

class MeshCoreSelfAnnouncementCore final : public SelfAnnouncementCore
{
  public:
    static bool buildAdvertPacket(const MeshCoreAnnouncementRequest& request,
                                  MeshCoreAnnouncementPacket* out_packet);
};

} // namespace chat::runtime

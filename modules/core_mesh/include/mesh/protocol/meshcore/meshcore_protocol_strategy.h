#pragma once

#include "mesh/protocol/mesh_protocol_strategy.h"

namespace mesh
{
namespace meshcore
{

class MeshCoreProtocolStrategy final : public MeshProtocolStrategy
{
  public:
    MeshProtocolKind kind() const override;
    RadioConfig deriveRadioConfig(const MeshRuntimeConfig& config) override;
    ProtocolResult buildDirectMessage(const ProtocolBuildContext& context,
                                      const DirectMessageCommand& command,
                                      EncodedPacket& out) override;
    ProtocolResult parseRadioPacket(const RadioRxPacket& packet,
                                    MeshProtocolEvent& out) override;

    void setLocalPublicHash(uint8_t public_hash);
    void setDirectSharedSecret(ByteView shared_secret);
    void clearDirectSharedSecret();
    void setGroupKey(ByteView key, uint8_t channel_hash = 0);
    void clearGroupKey();

  private:
    uint8_t local_public_hash_ = 0;
    uint8_t direct_key16_[16]{};
    uint8_t direct_key32_[32]{};
    bool has_direct_secret_ = false;
    uint8_t group_key16_[16]{};
    uint8_t group_key32_[32]{};
    uint8_t group_channel_hash_ = 0;
    bool has_group_key_ = false;
};

} // namespace meshcore
} // namespace mesh

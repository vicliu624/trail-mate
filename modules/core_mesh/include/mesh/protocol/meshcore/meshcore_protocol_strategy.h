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
};

} // namespace meshcore
} // namespace mesh

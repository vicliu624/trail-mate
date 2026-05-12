#include "mesh/protocol/meshcore/meshcore_protocol_strategy.h"

namespace mesh
{
namespace meshcore
{

MeshProtocolKind MeshCoreProtocolStrategy::kind() const
{
    return MeshProtocolKind::MeshCore;
}

RadioConfig MeshCoreProtocolStrategy::deriveRadioConfig(const MeshRuntimeConfig& config)
{
    return config.radio;
}

ProtocolResult MeshCoreProtocolStrategy::buildDirectMessage(
    const ProtocolBuildContext& context,
    const DirectMessageCommand& command,
    EncodedPacket& out)
{
    (void)context;
    (void)command;
    (void)out;
    return ProtocolResult::fail(ProtocolFailure::Unsupported);
}

ProtocolResult MeshCoreProtocolStrategy::parseRadioPacket(const RadioRxPacket& packet,
                                                          MeshProtocolEvent& out)
{
    (void)packet;
    (void)out;
    return ProtocolResult::fail(ProtocolFailure::Unsupported);
}

} // namespace meshcore
} // namespace mesh

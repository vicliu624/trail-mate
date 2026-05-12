#include "mesh/protocol/meshtastic/meshtastic_protocol_strategy.h"

namespace mesh
{
namespace meshtastic
{

MeshProtocolKind MeshtasticProtocolStrategy::kind() const
{
    return MeshProtocolKind::Meshtastic;
}

RadioConfig MeshtasticProtocolStrategy::deriveRadioConfig(const MeshRuntimeConfig& config)
{
    return config.radio;
}

ProtocolResult MeshtasticProtocolStrategy::buildDirectMessage(
    const ProtocolBuildContext& context,
    const DirectMessageCommand& command,
    EncodedPacket& out)
{
    (void)context;
    (void)command;
    (void)out;
    return ProtocolResult::fail(ProtocolFailure::Unsupported);
}

ProtocolResult MeshtasticProtocolStrategy::parseRadioPacket(const RadioRxPacket& packet,
                                                            MeshProtocolEvent& out)
{
    (void)packet;
    (void)out;
    return ProtocolResult::fail(ProtocolFailure::Unsupported);
}

} // namespace meshtastic
} // namespace mesh

#pragma once

#include "mesh/domain/direct_message.h"
#include "mesh/domain/local_identity.h"
#include "mesh/domain/mesh_message.h"
#include "mesh/domain/mesh_protocol_kind.h"
#include "mesh/domain/mesh_result.h"
#include "mesh/domain/peer_identity.h"
#include "mesh/domain/radio_packet.h"
#include "mesh/usecase/mesh_runtime_state.h"

namespace mesh
{

struct ProtocolBuildContext
{
    NodeId local_node{};
    LocalIdentity local_identity{};
    PeerPublicKey peer_key{};
    ByteView channel_key{};
    uint32_t now_ms = 0;
    uint32_t packet_id = 0;
    uint8_t channel_hash = 0;
    uint8_t hop_limit = 2;
    uint8_t route_type = 0;
    ByteView route_path{};
    bool has_air_want_ack = false;
    bool air_want_ack = false;
    bool include_payload_dest = true;
};

class MeshProtocolStrategy
{
  public:
    virtual ~MeshProtocolStrategy() = default;

    virtual MeshProtocolKind kind() const = 0;

    virtual RadioConfig deriveRadioConfig(const MeshRuntimeConfig& config) = 0;

    virtual ProtocolResult buildDirectMessage(const ProtocolBuildContext& context,
                                              const DirectMessageCommand& command,
                                              EncodedPacket& out) = 0;

    virtual ProtocolResult parseRadioPacket(const RadioRxPacket& packet,
                                            MeshProtocolEvent& out) = 0;
};

} // namespace mesh

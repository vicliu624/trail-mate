#pragma once

#include "mesh/domain/mesh_packet.h"
#include "mesh/domain/node_id.h"
#include "mesh/domain/peer_identity.h"

#include <stdint.h>

namespace mesh
{

enum class MeshProtocolEventKind : uint8_t
{
    None = 0,
    MessageReceived,
    PeerDiscovered,
    PeerKeyLearned,
    AckReceived,
    Duplicate,
};

struct MeshProtocolEvent
{
    MeshProtocolEventKind kind = MeshProtocolEventKind::None;
    NodeId peer{};
    PeerPublicKey peer_key{};
    ByteView payload{};
    uint32_t packet_id = 0;
};

} // namespace mesh

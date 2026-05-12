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
    uint8_t payload_bytes[256]{};
    size_t payload_size = 0;

    void setPayload(const uint8_t* data, size_t size)
    {
        payload = ByteView{};
        payload_size = 0;
        if (!data || size == 0 || size > sizeof(payload_bytes))
        {
            return;
        }
        for (size_t index = 0; index < size; ++index)
        {
            payload_bytes[index] = data[index];
        }
        payload_size = size;
        payload = ByteView{payload_bytes, payload_size};
    }
};

} // namespace mesh

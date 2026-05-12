#pragma once

#include "mesh/domain/mesh_packet.h"
#include "mesh/domain/mesh_result.h"
#include "mesh/domain/node_id.h"

namespace mesh
{
namespace meshtastic
{

struct MtPkiPayloadRequest
{
    ByteView shared_secret{};
    ByteView plaintext{};
    NodeId from{};
    uint32_t packet_id = 0;
    uint32_t extra_nonce = 0;
};

class MtPkiFlow
{
  public:
    ProtocolResult encryptWithSharedSecret(const MtPkiPayloadRequest& request,
                                           EncodedPacket& out) const;
};

} // namespace meshtastic
} // namespace mesh

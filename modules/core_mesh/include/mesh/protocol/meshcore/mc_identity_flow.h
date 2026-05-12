#pragma once

#include "mesh/domain/mesh_packet.h"
#include "mesh/domain/mesh_result.h"
#include "mesh/domain/node_id.h"

#include <stddef.h>
#include <stdint.h>

namespace mesh
{
namespace meshcore
{

constexpr size_t kMeshCorePublicKeySize = 32;
constexpr size_t kMeshCorePrivateKeySize = 64;
constexpr size_t kMeshCoreSignatureSize = 64;
constexpr size_t kMeshCoreSeedSize = 32;

struct MeshCoreKeyPairView
{
    const uint8_t* private_key = nullptr;
    size_t private_key_size = 0;
    const uint8_t* public_key = nullptr;
    size_t public_key_size = 0;
};

class McIdentityFlow
{
  public:
    ProtocolResult createKeypair(ByteView seed,
                                 uint8_t out_public_key[kMeshCorePublicKeySize],
                                 uint8_t out_private_key[kMeshCorePrivateKeySize]) const;
    ProtocolResult derivePublicKey(ByteView private_key,
                                   uint8_t out_public_key[kMeshCorePublicKeySize]) const;
    ProtocolResult sign(const MeshCoreKeyPairView& keypair,
                        ByteView message,
                        uint8_t out_signature[kMeshCoreSignatureSize]) const;
    ProtocolResult verify(ByteView public_key,
                          ByteView signature,
                          ByteView message) const;
    ProtocolResult deriveSharedSecret(ByteView private_key,
                                      ByteView peer_public_key,
                                      uint8_t out_secret[kMeshCorePublicKeySize]) const;
    NodeId deriveNodeId(ByteView public_key) const;
};

} // namespace meshcore
} // namespace mesh

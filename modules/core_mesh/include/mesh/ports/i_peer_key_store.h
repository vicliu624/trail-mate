#pragma once

#include "mesh/domain/mesh_result.h"
#include "mesh/domain/node_id.h"
#include "mesh/domain/peer_identity.h"

namespace mesh
{

class IPeerKeyStore
{
  public:
    virtual ~IPeerKeyStore() = default;

    virtual StoreResult get(NodeId node_id, PeerPublicKey& out) = 0;
    virtual StoreResult put(const PeerPublicKey& key) = 0;
    virtual StoreResult remove(NodeId node_id) = 0;
    virtual StoreResult clear() = 0;
};

} // namespace mesh

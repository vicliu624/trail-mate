#pragma once

#include "mesh/domain/local_identity.h"
#include "mesh/domain/mesh_result.h"
#include "mesh/domain/node_id.h"
#include "mesh/domain/peer_identity.h"
#include "mesh/ports/i_clock.h"
#include "mesh/ports/i_crypto_provider.h"
#include "mesh/ports/i_local_identity_store.h"
#include "mesh/ports/i_peer_key_store.h"

namespace mesh
{

class PeerIdentityService
{
  public:
    PeerIdentityService(ILocalIdentityStore& local_store,
                        IPeerKeyStore& peer_store,
                        ICryptoProvider& crypto,
                        IClock& clock);

    StoreResult ensureLocalIdentity(LocalIdentity& out);
    StoreResult findPeerKey(NodeId node_id, PeerPublicKey& out);
    StoreResult rememberPeerKey(const PeerPublicKey& key);

  private:
    ILocalIdentityStore& local_store_;
    IPeerKeyStore& peer_store_;
    ICryptoProvider& crypto_;
    IClock& clock_;
};

} // namespace mesh

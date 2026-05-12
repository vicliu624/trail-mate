#include "mesh/usecase/peer_identity_service.h"

#include <cstring>

namespace mesh
{

namespace
{

bool sameKey(const uint8_t left[32], const uint8_t right[32])
{
    return std::memcmp(left, right, 32) == 0;
}

} // namespace

PeerIdentityService::PeerIdentityService(ILocalIdentityStore& local_store,
                                         IPeerKeyStore& peer_store,
                                         ICryptoProvider& crypto,
                                         IClock& clock)
    : local_store_(local_store),
      peer_store_(peer_store),
      crypto_(crypto),
      clock_(clock)
{
}

StoreResult PeerIdentityService::ensureLocalIdentity(LocalIdentity& out)
{
    auto load = local_store_.load(out);
    if (load.ok && out.valid)
    {
        return StoreResult::success();
    }

    LocalIdentity generated{};
    auto random = crypto_.random(generated.private_key, sizeof(generated.private_key));
    if (!random.ok)
    {
        return StoreResult::fail(StoreFailure::IoError);
    }

    generated.valid = true;
    auto save = local_store_.save(generated);
    if (!save.ok)
    {
        return save;
    }

    out = generated;
    return StoreResult::success();
}

StoreResult PeerIdentityService::findPeerKey(NodeId node_id, PeerPublicKey& out)
{
    if (!node_id.isValidUnicast())
    {
        return StoreResult::fail(StoreFailure::InvalidArgument);
    }
    return peer_store_.get(node_id, out);
}

StoreResult PeerIdentityService::rememberPeerKey(const PeerPublicKey& key)
{
    if (!key.node_id.isValidUnicast())
    {
        return StoreResult::fail(StoreFailure::InvalidArgument);
    }

    PeerPublicKey current{};
    auto existing = peer_store_.get(key.node_id, current);
    const bool should_preserve_verification = existing.ok &&
                                              current.verified &&
                                              !key.verified;
    if (should_preserve_verification && !sameKey(current.public_key, key.public_key))
    {
        return StoreResult::fail(StoreFailure::PermissionDenied);
    }

    PeerPublicKey stored = key;
    if (should_preserve_verification)
    {
        stored.verified = true;
    }
    if (stored.updated_at_ms == 0)
    {
        stored.updated_at_ms = clock_.nowMs();
    }
    return peer_store_.put(stored);
}

} // namespace mesh

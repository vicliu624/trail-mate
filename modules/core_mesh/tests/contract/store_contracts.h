#pragma once

#include "mesh/ports/i_local_identity_store.h"
#include "mesh/ports/i_peer_key_store.h"

#include <cassert>
#include <cstring>

namespace mesh::tests::contract
{

inline LocalIdentity makeContractIdentity(uint8_t seed)
{
    LocalIdentity identity{};
    for (size_t index = 0; index < 32; ++index)
    {
        identity.public_key[index] = static_cast<uint8_t>(seed + index);
        identity.private_key[index] = static_cast<uint8_t>(seed + 0x40 + index);
    }
    identity.valid = true;
    return identity;
}

inline PeerPublicKey makeContractPeerKey(uint32_t node_id,
                                         uint8_t seed,
                                         bool verified)
{
    PeerPublicKey key{};
    key.node_id = NodeId{node_id};
    key.updated_at_ms = seed;
    key.verified = verified;
    for (size_t index = 0; index < 32; ++index)
    {
        key.public_key[index] = static_cast<uint8_t>(seed + index);
    }
    return key;
}

inline void runLocalIdentityStoreContract(ILocalIdentityStore& store)
{
    assert(store.clear().ok);

    LocalIdentity loaded{};
    auto missing = store.load(loaded);
    assert(!missing.ok);
    assert(missing.failure == StoreFailure::NotFound);

    auto identity = makeContractIdentity(0x11);
    assert(store.save(identity).ok);
    loaded = LocalIdentity{};
    assert(store.load(loaded).ok);
    assert(loaded.valid);
    assert(std::memcmp(loaded.public_key, identity.public_key, 32) == 0);
    assert(std::memcmp(loaded.private_key, identity.private_key, 32) == 0);

    auto replacement = makeContractIdentity(0x22);
    assert(store.save(replacement).ok);
    loaded = LocalIdentity{};
    assert(store.load(loaded).ok);
    assert(std::memcmp(loaded.public_key, replacement.public_key, 32) == 0);
    assert(std::memcmp(loaded.private_key, replacement.private_key, 32) == 0);

    assert(store.clear().ok);
    missing = store.load(loaded);
    assert(!missing.ok);
    assert(missing.failure == StoreFailure::NotFound);
}

inline void runPeerKeyStoreContract(IPeerKeyStore& store)
{
    assert(store.clear().ok);

    PeerPublicKey loaded{};
    auto missing = store.get(NodeId{0x1234}, loaded);
    assert(!missing.ok);
    assert(missing.failure == StoreFailure::NotFound);

    auto first = makeContractPeerKey(0x1234, 0x31, true);
    assert(store.put(first).ok);
    assert(store.get(NodeId{0x1234}, loaded).ok);
    assert(loaded.node_id == first.node_id);
    assert(loaded.verified == first.verified);
    assert(loaded.updated_at_ms == first.updated_at_ms);
    assert(std::memcmp(loaded.public_key, first.public_key, 32) == 0);

    auto second = makeContractPeerKey(0x5678, 0x41, false);
    assert(store.put(second).ok);
    assert(store.get(NodeId{0x5678}, loaded).ok);
    assert(loaded.node_id == second.node_id);
    assert(std::memcmp(loaded.public_key, second.public_key, 32) == 0);

    auto replacement = makeContractPeerKey(0x1234, 0x51, true);
    assert(store.put(replacement).ok);
    assert(store.get(NodeId{0x1234}, loaded).ok);
    assert(std::memcmp(loaded.public_key, replacement.public_key, 32) == 0);

    assert(store.remove(NodeId{0x1234}).ok);
    missing = store.get(NodeId{0x1234}, loaded);
    assert(!missing.ok);
    assert(missing.failure == StoreFailure::NotFound);

    assert(store.clear().ok);
    missing = store.get(NodeId{0x5678}, loaded);
    assert(!missing.ok);
    assert(missing.failure == StoreFailure::NotFound);
}

} // namespace mesh::tests::contract

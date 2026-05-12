#include "fake/fake_mesh_dependencies.h"
#include "mesh/usecase/peer_identity_service.h"

#include <cassert>
#include <cstring>

int main()
{
    mesh::tests::FakeClock clock;
    mesh::tests::FakeCryptoProvider crypto;
    mesh::tests::FakeLocalIdentityStore local_store;
    mesh::tests::FakePeerKeyStore peer_store;
    mesh::PeerIdentityService identity(local_store, peer_store, crypto, clock);

    mesh::LocalIdentity out{};
    assert(identity.ensureLocalIdentity(out).ok);
    assert(out.valid);
    assert(local_store.save_count == 1);
    assert(crypto.random_count == 1);

    out = mesh::LocalIdentity{};
    assert(identity.ensureLocalIdentity(out).ok);
    assert(out.valid);
    assert(local_store.save_count == 1);

    auto verified = mesh::tests::makePeerKey(0x1234, true);
    assert(identity.rememberPeerKey(verified).ok);

    auto unverified_changed = mesh::tests::makePeerKey(0x1234, false);
    unverified_changed.public_key[0] ^= 0x55;
    auto rejected = identity.rememberPeerKey(unverified_changed);
    assert(!rejected.ok);
    assert(rejected.failure == mesh::StoreFailure::PermissionDenied);

    mesh::PeerPublicKey loaded{};
    assert(identity.findPeerKey(mesh::NodeId{0x1234}, loaded).ok);
    assert(loaded.verified);
    assert(std::memcmp(loaded.public_key, verified.public_key, 32) == 0);

    clock.now_ms = 4242;
    auto unverified_same = verified;
    unverified_same.verified = false;
    unverified_same.updated_at_ms = 0;
    assert(identity.rememberPeerKey(unverified_same).ok);
    assert(identity.findPeerKey(mesh::NodeId{0x1234}, loaded).ok);
    assert(loaded.verified);
    assert(loaded.updated_at_ms == 4242);
    assert(std::memcmp(loaded.public_key, verified.public_key, 32) == 0);

    mesh::PeerPublicKey invalid{};
    invalid.node_id = mesh::NodeId{0};
    assert(!identity.rememberPeerKey(invalid).ok);
    return 0;
}

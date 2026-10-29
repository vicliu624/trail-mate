#include "chat/infra/mesh_peer_directory_core.h"
#include "chat/infra/meshcore/meshcore_protocol_helpers.h"
#include "chat/infra/voice/vmp_private_crypto.h"
#include "platform/linux/mesh/linux_meshcore_identity.h"
#include "platform/linux/mesh/linux_sqlite_mesh_identity_store.h"
#include "platform/linux/voice_contacts.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <mutex>
#include <unistd.h>

using namespace platform::linux_runtime;
namespace vmp = chat::voice::vmp;
using Protocol = chat::MeshProtocol;
struct Memory final : chat::IMeshPeerDirectoryBlobStore
{
    chat::MeshPeerDirectoryBlobLoadResult loadBlob(std::vector<uint8_t>&) override { return chat::MeshPeerDirectoryBlobLoadResult::Missing; }
    bool saveBlob(const uint8_t*, size_t) override { return false; }
    void clearBlob() override {}
};
chat::MeshPeerRecord peer(uint32_t node, const uint8_t key[32], bool verified)
{
    chat::MeshPeerRecord value;
    value.valid = true;
    value.identity.kind = chat::MeshPeerIdentityKind::NodeId;
    value.identity.protocol = Protocol::Meshtastic;
    value.identity.node_id = node;
    value.meshtastic.has_public_key = true;
    std::copy_n(key, 32, value.meshtastic.public_key);
    value.meshtastic.key_manually_verified = verified;
    return value;
}
int main()
{
    char root[] = "/tmp/trailmate-voice-contacts-XXXXXX";
    assert(mkdtemp(root));
    setenv("TRAIL_MATE_SETTINGS_ROOT", root, 1);
    vmp::EphemeralKeyPair alice, bob;
    assert(vmp::generateEphemeralKeyPair(&alice) && vmp::generateEphemeralKeyPair(&bob));
    std::array<uint8_t, 32> ab{}, ba{}, actual{}, invalid{};
    assert(LinuxVoiceContacts::deriveMeshtastic(alice.private_key, alice.public_key, bob.public_key, 100, 200, ab.data()));
    assert(LinuxVoiceContacts::deriveMeshtastic(bob.private_key, bob.public_key, alice.public_key, 200, 100, ba.data()) && ab == ba);
    assert(LinuxVoiceContacts::deriveMeshtastic(alice.private_key, alice.public_key, bob.public_key, 100, 201, actual.data()) && actual != ab);
    assert(!LinuxVoiceContacts::deriveMeshtastic(alice.private_key, bob.public_key, bob.public_key, 100, 200, actual.data()) && actual == invalid);
    assert(!LinuxVoiceContacts::deriveMeshtastic(alice.private_key, alice.public_key, invalid.data(), 100, 200, actual.data()) && actual == invalid);
    assert(!LinuxVoiceContacts::deriveMeshtastic(alice.private_key, alice.public_key, bob.public_key, 100, 100, actual.data()));
    ::mesh::LocalIdentity identity;
    identity.valid = true;
    std::copy_n(alice.private_key, 32, identity.private_key);
    std::copy_n(alice.public_key, 32, identity.public_key);
    platform::linux_runtime::mesh::LinuxSqliteLocalIdentityStore store;
    assert(store.save(identity).ok);
    Memory memory;
    chat::MeshPeerDirectoryCore::Options options;
    options.auto_save = false;
    chat::MeshPeerDirectoryCore directory(memory, options);
    assert(directory.begin().succeeded());
    auto contact = peer(200, bob.public_key, false);
    assert(directory.record(contact).succeeded());
    LinuxVoiceContacts contacts;
    assert(contacts.refresh(100, Protocol::Meshtastic, directory));
    assert(!contacts.contains(200, Protocol::Meshtastic));
    actual.fill(0xff);
    assert(!contacts.lookupVerifiedContactSecret(200, actual.data()) && actual == invalid);
    // Discovery and a favorite nickname do not establish trust.
    assert(directory.setUserAlias(contact.identity, "Bob").succeeded());
    assert(!contacts.refresh(100, Protocol::Meshtastic, directory));
    assert(!contacts.lookupVerifiedContactSecret(200, actual.data()));
    assert(directory.setKeyManuallyVerified(contact.identity, true).succeeded());
    assert(contacts.refresh(100, Protocol::Meshtastic, directory));
    assert(contacts.contains(200, Protocol::Meshtastic) && !contacts.contains(200, Protocol::MeshCore));
    assert(contacts.lookupVerifiedContactSecret(200, actual.data()) && actual == ab);
    assert(!contacts.refresh(100, Protocol::Meshtastic, directory));
    chat::MeshPeerUserFlags flags;
    flags.ignored = true;
    assert(directory.setUserFlags(contact.identity, flags).succeeded());
    assert(contacts.refresh(100, Protocol::Meshtastic, directory));
    actual.fill(0xff);
    assert(!contacts.lookupVerifiedContactSecret(200, actual.data()) && actual == invalid);
    flags.ignored = false;
    assert(directory.setUserFlags(contact.identity, flags).succeeded());
    assert(directory.setKeyManuallyVerified(contact.identity, false).succeeded());
    assert(!contacts.refresh(100, Protocol::Meshtastic, directory));
    assert(!contacts.lookupVerifiedContactSecret(200, actual.data()));
    assert(directory.setKeyManuallyVerified(contact.identity, true).succeeded());
    assert(contacts.refresh(100, Protocol::Meshtastic, directory));

    // Revocation during expensive identity work is observed before publishing
    // any derived key; frontend publication never waits for that derivation.
    std::mutex mutex;
    std::condition_variable wake;
    bool entered = false, release = false;
    LinuxVoiceContacts slow([&](Protocol, uint32_t, uint32_t, const uint8_t*, uint8_t out[32])
                            {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true; wake.notify_all();
        wake.wait(lock, [&] { return release; });
        std::fill_n(out, 32, 0x53); return true; });
    assert(slow.refresh(100, Protocol::Meshtastic, directory));
    auto pending = std::async(std::launch::async, [&]
                              { return slow.lookupVerifiedContactSecret(200, actual.data()); });
    {
        std::unique_lock<std::mutex> lock(mutex);
        assert(wake.wait_for(lock, std::chrono::seconds(2), [&]
                             { return entered; }));
    }
    assert(directory.setKeyManuallyVerified(contact.identity, false).succeeded());
    assert(slow.refresh(100, Protocol::Meshtastic, directory));
    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
        wake.notify_all();
    }
    assert(!pending.get() && actual == invalid);

    // Discovery cannot overwrite a manually pinned key. Replacement requires
    // explicit revocation followed by a new user verification.
    assert(contacts.refresh(100, Protocol::Meshtastic, directory));
    vmp::EphemeralKeyPair replacement;
    assert(vmp::generateEphemeralKeyPair(&replacement));
    assert(directory.record(peer(200, replacement.public_key, false)).succeeded());
    assert(!contacts.refresh(100, Protocol::Meshtastic, directory));
    assert(!contacts.lookupVerifiedContactSecret(200, actual.data()));
    assert(directory.setKeyManuallyVerified(contact.identity, true).succeeded());
    assert(contacts.refresh(100, Protocol::Meshtastic, directory));
    assert(contacts.lookupVerifiedContactSecret(200, actual.data()) && actual != ab);
    const auto newly_pinned = actual;
    assert(directory.record(peer(200, bob.public_key, false)).succeeded());
    assert(!contacts.refresh(100, Protocol::Meshtastic, directory));
    assert(contacts.lookupVerifiedContactSecret(200, actual.data()) && actual == newly_pinned);

    // The MC native identity uses its existing Ed25519 conversion, followed
    // by the same protocol/pair separation used by the production ESP peer.
    LinuxMeshCoreIdentity local;
    assert(local.begin());
    const auto other_root = std::filesystem::path(root) / "other";
    setenv("TRAIL_MATE_SETTINGS_ROOT", other_root.c_str(), 1);
    LinuxMeshCoreIdentity remote;
    assert(remote.begin());
    setenv("TRAIL_MATE_SETTINGS_ROOT", root, 1);
    chat::MeshPeerRecord mc;
    mc.valid = true;
    mc.identity.protocol = Protocol::MeshCore;
    mc.identity.kind = chat::MeshPeerIdentityKind::PublicKey;
    mc.identity.public_key_len = 32;
    std::copy(remote.publicKey().begin(), remote.publicKey().end(), mc.identity.public_key);
    mc.meshcore.node_id_hint = remote.nodeId();
    mc.meshcore.public_key_verified = true;
    assert(directory.record(mc).succeeded());
    assert(contacts.refresh(local.nodeId(), Protocol::MeshCore, directory));
    assert(!contacts.contains(200, Protocol::Meshtastic) && contacts.contains(remote.nodeId(), Protocol::MeshCore));
    std::array<uint8_t, 32> shared{};
    assert(remote.sharedSecret(local.publicKey().data(), shared.data()));
    assert(vmp::deriveVmpContactSecret(shared.data(), vmp::ContactSecretIdentityFamily::MeshCore, remote.nodeId(), local.nodeId(), ba.data()));
    assert(contacts.lookupVerifiedContactSecret(remote.nodeId(), actual.data()) && actual == ba);
    assert(contacts.refresh(local.nodeId() ^ 0x100, Protocol::MeshCore, directory));
    assert(!contacts.lookupVerifiedContactSecret(remote.nodeId(), actual.data()) && actual == invalid);
    assert(contacts.refresh(100, Protocol::Reticulum, directory));
    assert(!contacts.lookupVerifiedContactSecret(200, actual.data()));
    std::filesystem::remove_all(root);
}

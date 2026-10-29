#include "platform/linux/voice_contacts.h"
#include "platform/linux/mesh/linux_meshcore_identity.h"
#include "platform/linux/mesh/linux_sqlite_mesh_identity_store.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/evp.h>
#endif

namespace platform::linux_runtime
{
namespace
{
namespace vmp = ::chat::voice::vmp;
using Protocol = ::chat::MeshProtocol;
void clear(uint8_t* bytes, size_t size)
{
    if (!bytes) return;
    auto* cursor = static_cast<volatile uint8_t*>(bytes);
    while (size--) *cursor++ = 0;
}
struct Secret
{
    std::array<uint8_t, 32> bytes{};
    ~Secret() { clear(bytes.data(), bytes.size()); }
};
bool nativeIdentity(Protocol protocol, uint32_t self, uint32_t peer, const uint8_t key[32], uint8_t out[32])
{
    if (protocol == Protocol::Meshtastic)
    {
        ::mesh::LocalIdentity identity{};
        struct Cleanup
        {
            ::mesh::LocalIdentity& value;
            ~Cleanup() { clear(value.private_key, 32); }
        } cleanup{identity};
        ::platform::linux_runtime::mesh::LinuxSqliteLocalIdentityStore store;
        return store.load(identity).ok && identity.valid &&
               LinuxVoiceContacts::deriveMeshtastic(identity.private_key, identity.public_key, key, self, peer, out);
    }
    if (protocol == Protocol::MeshCore)
    {
        LinuxMeshCoreIdentity identity;
        Secret shared;
        return identity.begin() && identity.nodeId() == self && identity.sharedSecret(key, shared.bytes.data()) &&
               vmp::deriveVmpContactSecret(shared.bytes.data(), vmp::ContactSecretIdentityFamily::MeshCore, self, peer, out);
    }
    return false;
}
struct Entry
{
    uint32_t node = 0;
    std::array<uint8_t, 32> key{};
    bool operator==(const Entry& other) const { return node == other.node && key == other.key; }
};
} // namespace
struct LinuxVoiceContacts::Snapshot
{
    uint32_t self = 0;
    Protocol protocol = Protocol::Meshtastic;
    std::vector<Entry> entries;
};
LinuxVoiceContacts::LinuxVoiceContacts(Deriver derive) : snapshot_(std::make_shared<Snapshot>()), derive_(derive ? std::move(derive) : nativeIdentity) {}
bool LinuxVoiceContacts::refresh(uint32_t self, Protocol protocol, ::chat::IMeshPeerDirectory& directory)
{
    auto next = std::make_shared<Snapshot>();
    next->self = self;
    next->protocol = protocol;
    struct Visitor final : ::chat::IMeshPeerDirectoryVisitor
    {
        Snapshot& snapshot;
        bool valid = true;
        explicit Visitor(Snapshot& value) : snapshot(value) {}
        bool visit(const ::chat::MeshPeerRecord& record) override
        {
            if (!record.valid || record.flags.ignored || record.identity.protocol != snapshot.protocol) return true;
            Entry entry;
            entry.node = ::chat::meshPeerProjectedNodeId(record);
            if (!entry.node || entry.node == 0xffffffffu || entry.node == snapshot.self) return true;
            if (snapshot.protocol == Protocol::Meshtastic)
            {
                if (!record.meshtastic.has_public_key || !record.meshtastic.key_manually_verified) return true;
                std::copy_n(record.meshtastic.public_key, 32, entry.key.data());
            }
            else if (snapshot.protocol == Protocol::MeshCore)
            {
                if (!record.meshcore.public_key_verified) return true;
                if (record.meshcore.has_public_key) std::copy_n(record.meshcore.public_key, 32, entry.key.data());
                else if (record.identity.kind == ::chat::MeshPeerIdentityKind::PublicKey && record.identity.public_key_len == 32)
                    std::copy_n(record.identity.public_key, 32, entry.key.data());
                else return true;
                if (record.meshcore.has_public_key && record.identity.kind == ::chat::MeshPeerIdentityKind::PublicKey &&
                    (record.identity.public_key_len != 32 || !std::equal(entry.key.begin(), entry.key.end(), record.identity.public_key)))
                {
                    valid = false;
                    return false;
                }
            }
            else return true;
            if (std::all_of(entry.key.begin(), entry.key.end(), [](uint8_t byte)
                            { return byte == 0; })) return true;
            if (snapshot.entries.size() >= 1024)
            {
                valid = false;
                return false;
            }
            snapshot.entries.push_back(entry);
            return true;
        }
    } visitor(*next);
    if (self && (protocol == Protocol::Meshtastic || protocol == Protocol::MeshCore))
    {
        if (!directory.visit(protocol, ::chat::MeshPeerDirectoryView::All, visitor).succeeded() || !visitor.valid) next->entries.clear();
        std::sort(next->entries.begin(), next->entries.end(), [](const Entry& a, const Entry& b)
                  { return a.node < b.node; });
        for (size_t i = 1; i < next->entries.size(); ++i)
            if (next->entries[i - 1].node == next->entries[i].node && !(next->entries[i - 1] == next->entries[i]))
            {
                next->entries.clear();
                break;
            }
        next->entries.erase(std::unique(next->entries.begin(), next->entries.end()), next->entries.end());
    }
    const auto previous = std::atomic_load(&snapshot_);
    if (previous->self == next->self && previous->protocol == next->protocol && previous->entries == next->entries) return false;
    std::atomic_store(&snapshot_, std::shared_ptr<const Snapshot>(std::move(next)));
    return true;
}
bool LinuxVoiceContacts::contains(uint32_t peer, Protocol protocol) const
{
    const auto snapshot = std::atomic_load(&snapshot_);
    return snapshot->protocol == protocol && std::any_of(snapshot->entries.begin(), snapshot->entries.end(), [peer](const Entry& entry)
                                                         { return entry.node == peer; });
}
bool LinuxVoiceContacts::lookupVerifiedContactSecret(uint32_t peer, uint8_t out[32]) const
{
    if (!out) return false;
    clear(out, 32);
    const auto snapshot = std::atomic_load(&snapshot_);
    const auto entry = std::find_if(snapshot->entries.begin(), snapshot->entries.end(), [peer](const Entry& entry)
                                    { return entry.node == peer; });
    if (entry == snapshot->entries.end()) return false;
    Secret result;
    try
    {
        if (!derive_(snapshot->protocol, snapshot->self, peer, entry->key.data(), result.bytes.data()) ||
            std::atomic_load(&snapshot_) != snapshot ||
            std::all_of(result.bytes.begin(), result.bytes.end(), [](uint8_t byte)
                        { return byte == 0; })) return false;
    }
    catch (...)
    {
        return false;
    }
    std::copy(result.bytes.begin(), result.bytes.end(), out);
    return true;
}
bool LinuxVoiceContacts::deriveMeshtastic(const uint8_t private_key[32], const uint8_t public_key[32],
                                          const uint8_t peer_key[32], uint32_t self, uint32_t peer, uint8_t out[32])
{
    if (!out) return false;
    clear(out, 32);
#if defined(TRAIL_MATE_HAS_OPENSSL)
    if (!private_key || !public_key || !peer_key || !self || !peer || self == peer || self == 0xffffffffu || peer == 0xffffffffu) return false;
    auto* local = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, private_key, 32);
    auto* remote = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, peer_key, 32);
    auto* context = local ? EVP_PKEY_CTX_new(local, nullptr) : nullptr;
    std::array<uint8_t, 32> actual{};
    size_t size = actual.size();
    Secret shared;
    const bool valid = local && remote && context && EVP_PKEY_get_raw_public_key(local, actual.data(), &size) == 1 && size == 32 &&
                       std::equal(actual.begin(), actual.end(), public_key) && EVP_PKEY_derive_init(context) == 1 &&
                       EVP_PKEY_derive_set_peer(context, remote) == 1 && (size = 32, EVP_PKEY_derive(context, shared.bytes.data(), &size) == 1) && size == 32 &&
                       vmp::deriveVmpContactSecret(shared.bytes.data(), vmp::ContactSecretIdentityFamily::Meshtastic, self, peer, out);
    EVP_PKEY_CTX_free(context);
    EVP_PKEY_free(remote);
    EVP_PKEY_free(local);
    return valid;
#else
    (void)private_key;
    (void)public_key;
    (void)peer_key;
    (void)self;
    (void)peer;
    return false;
#endif
}
} // namespace platform::linux_runtime

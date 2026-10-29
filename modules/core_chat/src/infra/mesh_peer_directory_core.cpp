#include "chat/infra/mesh_peer_directory_core.h"

#include <algorithm>
#include <cstring>
#include <utility>

#if defined(_MSC_VER)
#define TRAILMATE_PACK_PUSH __pragma(pack(push, 1))
#define TRAILMATE_PACK_POP __pragma(pack(pop))
#define TRAILMATE_PACKED
#else
#define TRAILMATE_PACK_PUSH
#define TRAILMATE_PACK_POP
#define TRAILMATE_PACKED __attribute__((packed))
#endif

namespace chat
{

namespace
{

constexpr uint32_t kMeshPeerDirectoryMagic = 0x5244504DUL; // MPDR
constexpr uint8_t kMeshPeerDirectoryPersistVersionV1 = 1;
constexpr uint8_t kMeshPeerDirectoryPersistVersion = 2;

TRAILMATE_PACK_PUSH
struct PersistedMeshPeerDirectoryHeaderV1
{
    uint32_t magic = kMeshPeerDirectoryMagic;
    uint8_t version = kMeshPeerDirectoryPersistVersion;
    uint8_t reserved[3] = {};
    uint32_t count = 0;
    uint32_t crc = 0;
} TRAILMATE_PACKED;
TRAILMATE_PACK_POP

TRAILMATE_PACK_PUSH
struct PersistedMeshPeerNodeFactsV1
{
    char short_name[kMeshPeerShortNameMaxLen] = {};
    char long_name[kMeshPeerDisplayNameMaxLen] = {};
    uint8_t role = 0xFF;
    uint8_t hw_model = 0;
    uint8_t channel = 0xFF;
    uint8_t hops_away = 0xFF;
    uint8_t has_macaddr = 0;
    uint8_t via_mqtt = 0;
    uint8_t reserved[2] = {};
    uint8_t macaddr[kMeshPeerMacAddrLen] = {};
    uint8_t reserved_mac[2] = {};
} TRAILMATE_PACKED;
TRAILMATE_PACK_POP

TRAILMATE_PACK_PUSH
struct PersistedMeshPeerEntryV1
{
    uint8_t valid = 0;
    uint8_t protocol = 0;
    uint8_t identity_kind = 0;
    uint8_t source = 0;
    uint32_t node_id = 0;
    uint8_t identity_public_key_len = 0;
    uint8_t reserved_identity[3] = {};
    uint8_t identity_public_key[kMeshPeerPublicKeyMaxLen] = {};
    uint8_t identity_reticulum_valid = 0;
    uint8_t reserved_reticulum_identity[3] = {};
    uint8_t reticulum_destination_hash[kReticulumPeerHashSize] = {};
    uint8_t reticulum_identity_hash[kReticulumPeerHashSize] = {};
    uint32_t first_seen_s = 0;
    uint32_t last_seen_s = 0;
    char display_name[kMeshPeerDisplayNameMaxLen] = {};
    uint8_t favorite = 0;
    uint8_t ignored = 0;
    uint8_t trusted = 0;
    uint8_t reserved_flags = 0;

    PersistedMeshPeerNodeFactsV1 meshtastic_node{};
    uint8_t meshtastic_has_public_key = 0;
    uint8_t meshtastic_key_verified = 0;
    uint8_t meshtastic_reserved[2] = {};
    uint8_t meshtastic_public_key[kMeshPeerMeshtasticPublicKeyLen] = {};

    PersistedMeshPeerNodeFactsV1 meshcore_node{};
    uint8_t meshcore_has_public_key = 0;
    uint8_t meshcore_key_verified = 0;
    uint8_t meshcore_has_peer_hash = 0;
    uint8_t meshcore_peer_hash = 0;
    uint8_t meshcore_has_next_hop = 0;
    uint8_t meshcore_next_hop = 0;
    uint8_t meshcore_reserved[2] = {};
    uint8_t meshcore_public_key[kMeshPeerMeshCorePublicKeyLen] = {};

    uint8_t reticulum_identity_valid = 0;
    uint8_t reticulum_has_public_keys = 0;
    uint8_t reticulum_delivery = 0;
    uint8_t reticulum_propagation = 0;
    uint8_t reticulum_destination_hash2[kReticulumPeerHashSize] = {};
    uint8_t reticulum_identity_hash2[kReticulumPeerHashSize] = {};
    uint8_t reticulum_enc_pub[kMeshPeerReticulumPublicKeyLen] = {};
    uint8_t reticulum_sig_pub[kMeshPeerReticulumPublicKeyLen] = {};
    uint8_t reticulum_has_ratchet = 0;
    uint8_t reticulum_reserved[3] = {};
    uint32_t reticulum_ratchet_seen_s = 0;
    uint8_t reticulum_ratchet_pub[kMeshPeerReticulumRatchetLen] = {};
} TRAILMATE_PACKED;
TRAILMATE_PACK_POP

TRAILMATE_PACK_PUSH
struct PersistedMeshPeerEntryV2
{
    PersistedMeshPeerEntryV1 peer{};
    char user_alias[kMeshPeerUserAliasMaxLen + 1] = {};
} TRAILMATE_PACKED;
TRAILMATE_PACK_POP

void copyIntoPersisted(PersistedMeshPeerNodeFactsV1& dst,
                       const MeshPeerNodeFacts& src)
{
    dst = {};
    std::memcpy(dst.short_name, src.short_name, sizeof(dst.short_name));
    dst.short_name[sizeof(dst.short_name) - 1] = '\0';
    std::memcpy(dst.long_name, src.long_name, sizeof(dst.long_name));
    dst.long_name[sizeof(dst.long_name) - 1] = '\0';
    dst.role = src.role;
    dst.hw_model = src.hw_model;
    dst.channel = src.channel;
    dst.hops_away = src.hops_away;
    dst.has_macaddr = src.has_macaddr ? 1U : 0U;
    dst.via_mqtt = src.via_mqtt ? 1U : 0U;
    std::memcpy(dst.macaddr, src.macaddr, sizeof(dst.macaddr));
}

void copyFromPersisted(MeshPeerNodeFacts& dst,
                       const PersistedMeshPeerNodeFactsV1& src)
{
    dst = {};
    std::memcpy(dst.short_name, src.short_name, sizeof(dst.short_name));
    dst.short_name[sizeof(dst.short_name) - 1] = '\0';
    std::memcpy(dst.long_name, src.long_name, sizeof(dst.long_name));
    dst.long_name[sizeof(dst.long_name) - 1] = '\0';
    dst.role = src.role;
    dst.hw_model = src.hw_model;
    dst.channel = src.channel;
    dst.hops_away = src.hops_away;
    dst.has_macaddr = src.has_macaddr != 0;
    dst.via_mqtt = src.via_mqtt != 0;
    std::memcpy(dst.macaddr, src.macaddr, sizeof(dst.macaddr));
}

void copyReticulumIdentityBytes(ReticulumPeerIdentity& dst,
                                uint8_t valid,
                                const uint8_t destination[kReticulumPeerHashSize],
                                const uint8_t identity[kReticulumPeerHashSize])
{
    dst = {};
    if (valid == 0)
    {
        return;
    }
    dst = makeReticulumPeerIdentity(destination, identity);
}

void copyIntoPersisted(PersistedMeshPeerEntryV1& dst,
                       const MeshPeerRecord& src)
{
    dst = {};
    dst.valid = src.valid ? 1U : 0U;
    dst.protocol = static_cast<uint8_t>(src.identity.protocol);
    dst.identity_kind = static_cast<uint8_t>(src.identity.kind);
    dst.source = static_cast<uint8_t>(src.source);
    dst.node_id = src.identity.node_id;
    dst.identity_public_key_len = src.identity.public_key_len;
    std::memcpy(dst.identity_public_key,
                src.identity.public_key,
                sizeof(dst.identity_public_key));
    dst.identity_reticulum_valid = src.identity.reticulum.valid ? 1U : 0U;
    (void)copyReticulumIdentityHashes(dst.reticulum_destination_hash,
                                      dst.reticulum_identity_hash,
                                      src.identity.reticulum);
    dst.first_seen_s = src.first_seen_s;
    dst.last_seen_s = src.last_seen_s;
    std::memcpy(dst.display_name, src.display_name, sizeof(dst.display_name));
    dst.display_name[sizeof(dst.display_name) - 1] = '\0';
    dst.favorite = src.flags.favorite ? 1U : 0U;
    dst.ignored = src.flags.ignored ? 1U : 0U;
    dst.trusted = src.flags.trusted ? 1U : 0U;

    copyIntoPersisted(dst.meshtastic_node, src.meshtastic.node);
    dst.meshtastic_has_public_key = src.meshtastic.has_public_key ? 1U : 0U;
    dst.meshtastic_key_verified =
        src.meshtastic.key_manually_verified ? 1U : 0U;
    std::memcpy(dst.meshtastic_public_key,
                src.meshtastic.public_key,
                sizeof(dst.meshtastic_public_key));

    copyIntoPersisted(dst.meshcore_node, src.meshcore.node);
    dst.meshcore_has_public_key = src.meshcore.has_public_key ? 1U : 0U;
    dst.meshcore_key_verified = src.meshcore.public_key_verified ? 1U : 0U;
    dst.meshcore_has_peer_hash = src.meshcore.has_peer_hash ? 1U : 0U;
    dst.meshcore_peer_hash = src.meshcore.peer_hash;
    dst.meshcore_has_next_hop = src.meshcore.has_next_hop ? 1U : 0U;
    dst.meshcore_next_hop = src.meshcore.next_hop;
    std::memcpy(dst.meshcore_public_key,
                src.meshcore.public_key,
                sizeof(dst.meshcore_public_key));

    dst.reticulum_identity_valid = src.reticulum.identity.valid ? 1U : 0U;
    dst.reticulum_has_public_keys = src.reticulum.has_public_keys ? 1U : 0U;
    dst.reticulum_delivery = src.reticulum.delivery ? 1U : 0U;
    dst.reticulum_propagation = src.reticulum.propagation ? 1U : 0U;
    (void)copyReticulumIdentityHashes(dst.reticulum_destination_hash2,
                                      dst.reticulum_identity_hash2,
                                      src.reticulum.identity);
    std::memcpy(dst.reticulum_enc_pub,
                src.reticulum.enc_pub,
                sizeof(dst.reticulum_enc_pub));
    std::memcpy(dst.reticulum_sig_pub,
                src.reticulum.sig_pub,
                sizeof(dst.reticulum_sig_pub));
    dst.reticulum_has_ratchet = src.reticulum.has_ratchet ? 1U : 0U;
    dst.reticulum_ratchet_seen_s = src.reticulum.ratchet_seen_s;
    std::memcpy(dst.reticulum_ratchet_pub,
                src.reticulum.ratchet_pub,
                sizeof(dst.reticulum_ratchet_pub));
}

bool copyFromPersisted(MeshPeerRecord& dst,
                       const PersistedMeshPeerEntryV1& src)
{
    dst = {};
    dst.valid = src.valid != 0;
    dst.identity.protocol = static_cast<MeshProtocol>(src.protocol);
    dst.identity.kind = static_cast<MeshPeerIdentityKind>(src.identity_kind);
    dst.identity.node_id = src.node_id;
    dst.identity.public_key_len = src.identity_public_key_len;
    if (dst.identity.public_key_len > kMeshPeerPublicKeyMaxLen)
    {
        return false;
    }
    std::memcpy(dst.identity.public_key,
                src.identity_public_key,
                sizeof(dst.identity.public_key));
    copyReticulumIdentityBytes(dst.identity.reticulum,
                               src.identity_reticulum_valid,
                               src.reticulum_destination_hash,
                               src.reticulum_identity_hash);
    dst.source = static_cast<MeshPeerSource>(src.source);
    dst.first_seen_s = src.first_seen_s;
    dst.last_seen_s = src.last_seen_s;
    std::memcpy(dst.display_name, src.display_name, sizeof(dst.display_name));
    dst.display_name[sizeof(dst.display_name) - 1] = '\0';
    dst.flags.favorite = src.favorite != 0;
    dst.flags.ignored = src.ignored != 0;
    dst.flags.trusted = src.trusted != 0;

    copyFromPersisted(dst.meshtastic.node, src.meshtastic_node);
    dst.meshtastic.has_public_key = src.meshtastic_has_public_key != 0;
    dst.meshtastic.key_manually_verified = src.meshtastic_key_verified != 0;
    std::memcpy(dst.meshtastic.public_key,
                src.meshtastic_public_key,
                sizeof(dst.meshtastic.public_key));

    copyFromPersisted(dst.meshcore.node, src.meshcore_node);
    dst.meshcore.has_public_key = src.meshcore_has_public_key != 0;
    dst.meshcore.public_key_verified = src.meshcore_key_verified != 0;
    dst.meshcore.has_peer_hash = src.meshcore_has_peer_hash != 0;
    dst.meshcore.peer_hash = src.meshcore_peer_hash;
    dst.meshcore.has_next_hop = src.meshcore_has_next_hop != 0;
    dst.meshcore.next_hop = src.meshcore_next_hop;
    std::memcpy(dst.meshcore.public_key,
                src.meshcore_public_key,
                sizeof(dst.meshcore.public_key));

    copyReticulumIdentityBytes(dst.reticulum.identity,
                               src.reticulum_identity_valid,
                               src.reticulum_destination_hash2,
                               src.reticulum_identity_hash2);
    dst.reticulum.has_public_keys = src.reticulum_has_public_keys != 0;
    dst.reticulum.delivery = src.reticulum_delivery != 0;
    dst.reticulum.propagation = src.reticulum_propagation != 0;
    std::memcpy(dst.reticulum.enc_pub,
                src.reticulum_enc_pub,
                sizeof(dst.reticulum.enc_pub));
    std::memcpy(dst.reticulum.sig_pub,
                src.reticulum_sig_pub,
                sizeof(dst.reticulum.sig_pub));
    dst.reticulum.has_ratchet =
        src.reticulum_has_ratchet != 0 &&
        meshPeerHasNonZeroBytes(src.reticulum_ratchet_pub,
                                sizeof(src.reticulum_ratchet_pub));
    if (dst.reticulum.has_ratchet)
    {
        std::memcpy(dst.reticulum.ratchet_pub,
                    src.reticulum_ratchet_pub,
                    sizeof(dst.reticulum.ratchet_pub));
        dst.reticulum.ratchet_seen_s = src.reticulum_ratchet_seen_s;
    }
    return meshPeerRecordIsValid(dst);
}

void copyIntoPersisted(PersistedMeshPeerEntryV2& dst,
                       const MeshPeerRecord& src)
{
    dst = {};
    copyIntoPersisted(dst.peer, src);
    copyMeshPeerText(dst.user_alias, sizeof(dst.user_alias), src.user_alias);
}

bool copyFromPersisted(MeshPeerRecord& dst,
                       const PersistedMeshPeerEntryV2& src)
{
    if (!copyFromPersisted(dst, src.peer))
    {
        return false;
    }
    copyMeshPeerText(dst.user_alias,
                     sizeof(dst.user_alias),
                     src.user_alias);
    return true;
}

bool textContains(const char* haystack, const char* needle)
{
    return haystack && needle && std::strstr(haystack, needle) != nullptr;
}

bool hasMeshPeerText(const char* text)
{
    return text && text[0] != '\0';
}

bool matchesDirectoryView(const MeshPeerRecord& record,
                          MeshPeerDirectoryView view)
{
    const bool contact = meshPeerIsContact(record);
    switch (view)
    {
    case MeshPeerDirectoryView::Contacts:
        return contact;
    case MeshPeerDirectoryView::Nearby:
        return !contact && !record.flags.ignored;
    case MeshPeerDirectoryView::Ignored:
        return !contact && record.flags.ignored;
    case MeshPeerDirectoryView::All:
    default:
        return true;
    }
}

void copyNonEmptyMeshPeerText(char* out, std::size_t out_len, const char* text)
{
    if (hasMeshPeerText(text))
    {
        copyMeshPeerText(out, out_len, text);
    }
}

bool hasNodeFacts(const MeshPeerNodeFacts& facts)
{
    return hasMeshPeerText(facts.short_name) ||
           hasMeshPeerText(facts.long_name) ||
           facts.role != 0xFF ||
           facts.hw_model != 0 ||
           facts.channel != 0xFF ||
           facts.hops_away != 0xFF ||
           facts.has_macaddr ||
           facts.via_mqtt;
}

void mergeNodeFacts(MeshPeerNodeFacts& dst, const MeshPeerNodeFacts& src)
{
    const bool source_has_facts = hasNodeFacts(src);
    copyNonEmptyMeshPeerText(dst.short_name, sizeof(dst.short_name), src.short_name);
    copyNonEmptyMeshPeerText(dst.long_name, sizeof(dst.long_name), src.long_name);
    if (src.role != 0xFF)
    {
        dst.role = src.role;
    }
    if (src.hw_model != 0)
    {
        dst.hw_model = src.hw_model;
    }
    if (src.channel != 0xFF)
    {
        dst.channel = src.channel;
    }
    if (src.hops_away != 0xFF)
    {
        dst.hops_away = src.hops_away;
    }
    if (src.has_macaddr)
    {
        dst.has_macaddr = true;
        std::memcpy(dst.macaddr, src.macaddr, sizeof(dst.macaddr));
    }
    if (source_has_facts)
    {
        dst.via_mqtt = src.via_mqtt;
    }
}

void mergeMeshtasticFacts(MeshtasticPeerFacts& dst,
                          const MeshtasticPeerFacts& src)
{
    mergeNodeFacts(dst.node, src.node);
    if (src.has_next_hop)
    {
        dst.has_next_hop = true;
        dst.next_hop = src.next_hop;
    }
    if (src.has_public_key &&
        meshPeerHasNonZeroBytes(src.public_key,
                                kMeshPeerMeshtasticPublicKeyLen))
    {
        const bool changed =
            !dst.has_public_key ||
            std::memcmp(dst.public_key,
                        src.public_key,
                        kMeshPeerMeshtasticPublicKeyLen) != 0;
        if (changed && dst.has_public_key && dst.key_manually_verified)
        {
            return;
        }
        dst.has_public_key = true;
        std::memcpy(dst.public_key,
                    src.public_key,
                    kMeshPeerMeshtasticPublicKeyLen);
        dst.key_manually_verified =
            changed ? src.key_manually_verified
                    : (dst.key_manually_verified || src.key_manually_verified);
    }
}

void mergeMeshCoreFacts(MeshCorePeerFacts& dst,
                        const MeshCorePeerFacts& src)
{
    mergeNodeFacts(dst.node, src.node);
    if (src.has_public_key &&
        meshPeerHasNonZeroBytes(src.public_key,
                                kMeshPeerMeshCorePublicKeyLen))
    {
        const bool changed =
            !dst.has_public_key ||
            std::memcmp(dst.public_key,
                        src.public_key,
                        kMeshPeerMeshCorePublicKeyLen) != 0;
        if (changed && dst.has_public_key && dst.public_key_verified)
        {
            return;
        }
        dst.has_public_key = true;
        std::memcpy(dst.public_key,
                    src.public_key,
                    kMeshPeerMeshCorePublicKeyLen);
        dst.public_key_verified =
            changed ? src.public_key_verified
                    : (dst.public_key_verified || src.public_key_verified);
    }
    if (src.has_peer_hash)
    {
        dst.has_peer_hash = true;
        dst.peer_hash = src.peer_hash;
    }
    if (src.has_next_hop)
    {
        dst.has_next_hop = true;
        dst.next_hop = src.next_hop;
    }
    if (src.node_id_hint != 0)
    {
        dst.node_id_hint = src.node_id_hint;
    }
}

void mergeReticulumFacts(ReticulumPeerFacts& dst,
                         const ReticulumPeerFacts& src)
{
    if (src.identity.valid)
    {
        dst.identity = src.identity;
    }
    if (src.has_public_keys &&
        meshPeerHasNonZeroBytes(src.enc_pub, kMeshPeerReticulumPublicKeyLen) &&
        meshPeerHasNonZeroBytes(src.sig_pub, kMeshPeerReticulumPublicKeyLen))
    {
        dst.has_public_keys = true;
        std::memcpy(dst.enc_pub, src.enc_pub, sizeof(dst.enc_pub));
        std::memcpy(dst.sig_pub, src.sig_pub, sizeof(dst.sig_pub));
    }
    if (src.has_ratchet &&
        meshPeerHasNonZeroBytes(src.ratchet_pub, kMeshPeerReticulumRatchetLen))
    {
        dst.has_ratchet = true;
        std::memcpy(dst.ratchet_pub,
                    src.ratchet_pub,
                    sizeof(dst.ratchet_pub));
        dst.ratchet_seen_s = src.ratchet_seen_s;
    }
    dst.delivery = dst.delivery || src.delivery;
    dst.propagation = dst.propagation || src.propagation;
}

MeshPeerSource mergePeerSource(MeshPeerSource existing,
                               MeshPeerSource incoming)
{
    if (incoming == MeshPeerSource::Unknown)
    {
        return existing;
    }
    if (incoming == MeshPeerSource::Manual ||
        incoming == MeshPeerSource::Import)
    {
        return incoming;
    }
    if (existing == MeshPeerSource::Manual ||
        existing == MeshPeerSource::Import)
    {
        return existing;
    }
    if (existing == MeshPeerSource::Unknown)
    {
        return incoming;
    }
    return incoming;
}

MeshPeerRecord mergePeerRecordFactsImpl(const MeshPeerRecord& existing,
                                        const MeshPeerRecord& incoming)
{
    MeshPeerRecord next = existing;
    next.valid = existing.valid || incoming.valid;
    next.source = mergePeerSource(existing.source, incoming.source);
    if (existing.first_seen_s != 0 && incoming.first_seen_s != 0)
    {
        next.first_seen_s = std::min(existing.first_seen_s, incoming.first_seen_s);
    }
    else
    {
        next.first_seen_s = existing.first_seen_s != 0
                                ? existing.first_seen_s
                                : incoming.first_seen_s;
    }
    next.last_seen_s = std::max(existing.last_seen_s, incoming.last_seen_s);
    if (next.last_seen_s < next.first_seen_s)
    {
        next.last_seen_s = next.first_seen_s;
    }
    copyNonEmptyMeshPeerText(next.display_name,
                             sizeof(next.display_name),
                             incoming.display_name);
    next.flags = existing.flags;
    if (incoming.observations.has_snr)
    {
        next.observations.has_snr = true;
        next.observations.snr = incoming.observations.snr;
    }
    if (incoming.observations.has_rssi)
    {
        next.observations.has_rssi = true;
        next.observations.rssi = incoming.observations.rssi;
    }
    if (incoming.observations.has_device_metrics)
    {
        next.observations.has_device_metrics = true;
        next.observations.device_metrics =
            incoming.observations.device_metrics;
    }
    if (incoming.observations.has_position)
    {
        next.observations.has_position = true;
        next.observations.position = incoming.observations.position;
    }
    mergeMeshtasticFacts(next.meshtastic, incoming.meshtastic);
    mergeMeshCoreFacts(next.meshcore, incoming.meshcore);
    mergeReticulumFacts(next.reticulum, incoming.reticulum);
    return next;
}

NodeId reticulumNodeIdFromDestinationHash(const uint8_t* destination_hash)
{
    if (!destination_hash)
    {
        return 0;
    }
    return (static_cast<NodeId>(destination_hash[12]) << 24) |
           (static_cast<NodeId>(destination_hash[13]) << 16) |
           (static_cast<NodeId>(destination_hash[14]) << 8) |
           static_cast<NodeId>(destination_hash[15]);
}

} // namespace

MeshPeerDirectoryBlobLoadResult IMeshPeerDirectoryBlobStore::loadBlobTo(
    IMeshPeerDirectoryBlobSink& sink)
{
    std::vector<uint8_t> buffer;
    const MeshPeerDirectoryBlobLoadResult result = loadBlob(buffer);
    if (result != MeshPeerDirectoryBlobLoadResult::Loaded)
    {
        return result;
    }
    if (!sink.begin(buffer.size()))
    {
        return MeshPeerDirectoryBlobLoadResult::IoError;
    }
    if (!buffer.empty() && !sink.write(buffer.data(), buffer.size()))
    {
        return MeshPeerDirectoryBlobLoadResult::IoError;
    }
    return sink.finish() ? MeshPeerDirectoryBlobLoadResult::Loaded
                         : MeshPeerDirectoryBlobLoadResult::IoError;
}

MeshPeerRecord mergeMeshPeerRecordFacts(const MeshPeerRecord& existing,
                                        const MeshPeerRecord& incoming)
{
    return mergePeerRecordFactsImpl(existing, incoming);
}

MeshPeerDirectoryCore::MeshPeerDirectoryCore(
    IMeshPeerDirectoryBlobStore& blob_store)
    : blob_store_(blob_store)
{
}

MeshPeerDirectoryCore::MeshPeerDirectoryCore(
    IMeshPeerDirectoryBlobStore& blob_store,
    const Options& options)
    : blob_store_(blob_store), options_(options)
{
}

void MeshPeerDirectoryCore::setAutoSaveEnabled(bool enabled)
{
    options_.auto_save = enabled;
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::begin()
{
    std::vector<uint8_t> blob;
    const auto loaded = loadPersistenceBlob(blob);
    if (loaded == MeshPeerDirectoryBlobLoadResult::Unavailable)
    {
        begun_ = false;
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::StorageUnavailable);
    }
    if (loaded == MeshPeerDirectoryBlobLoadResult::IoError)
    {
        begun_ = false;
        return MeshPeerDirectoryStatus::fail(MeshPeerDirectoryStatusCode::IoError);
    }

    const MeshPeerDirectoryStatus empty_status = beginEmpty();
    if (!empty_status.succeeded())
    {
        return empty_status;
    }
    if (loaded == MeshPeerDirectoryBlobLoadResult::Missing || blob.empty())
    {
        return MeshPeerDirectoryStatus::success();
    }

    return hydratePersistenceBlob(blob.data(), blob.size());
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::beginEmpty()
{
    records_.clear();
    dirty_ = false;
    begun_ = true;
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryBlobLoadResult MeshPeerDirectoryCore::loadPersistenceBlob(
    std::vector<uint8_t>& out) const
{
    return blob_store_.loadBlob(out);
}

MeshPeerDirectoryBlobLoadResult MeshPeerDirectoryCore::streamPersistenceBlob(
    IMeshPeerDirectoryBlobSink& sink) const
{
    return blob_store_.loadBlobTo(sink);
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::hydratePersistenceBlob(
    const uint8_t* data,
    std::size_t len)
{
    if (!begun_ || (!data && len != 0U))
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::InvalidArgument);
    }
    if (len == 0U)
    {
        records_.clear();
        dirty_ = false;
        return MeshPeerDirectoryStatus::success();
    }

    std::vector<MeshPeerRecord> decoded;
    if (!decodeBlob(decoded, data, len))
    {
        records_.clear();
        dirty_ = false;
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::IoError);
    }
    records_ = std::move(decoded);
    dirty_ = false;
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::record(
    const MeshPeerRecord& record)
{
    if (!begun_ || !meshPeerRecordIsValid(record))
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::InvalidArgument);
    }

    const std::size_t existing_index = findIndex(record.identity);
    if (existing_index < records_.size())
    {
        const MeshPeerRecord& existing = records_[existing_index];
        MeshPeerRecord next = mergeMeshPeerRecordFacts(existing, record);
        records_[existing_index] = next;
        markDirty();
        maybeSave();
        return MeshPeerDirectoryStatus::success();
    }

    const auto capacity = capacityFor(record.identity.protocol);
    if (capacity.persisted_records != 0 &&
        countForProtocol(record.identity.protocol) >= capacity.persisted_records)
    {
        evictOldest(record.identity.protocol);
    }

    MeshPeerRecord next = record;
    next.user_alias[0] = '\0';
    next.flags = {};
    if (next.last_seen_s < next.first_seen_s)
    {
        next.last_seen_s = next.first_seen_s;
    }
    records_.push_back(next);
    markDirty();
    maybeSave();
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::find(
    const MeshPeerIdentity& identity,
    MeshPeerRecord& out_record)
{
    const std::size_t index = findIndex(identity);
    if (index >= records_.size())
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::NotFound);
    }
    out_record = records_[index];
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::findByNodeId(
    MeshProtocol protocol,
    NodeId node_id,
    MeshPeerRecord& out_record)
{
    if (node_id == 0)
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::InvalidArgument);
    }

    for (const auto& record : records_)
    {
        if (!meshPeerSameProtocol(record.identity.protocol, protocol))
        {
            continue;
        }

        if (record.identity.kind == MeshPeerIdentityKind::NodeId &&
            record.identity.node_id == node_id)
        {
            out_record = record;
            return MeshPeerDirectoryStatus::success();
        }

        if (protocol == MeshProtocol::MeshCore &&
            record.meshcore.node_id_hint == node_id)
        {
            out_record = record;
            return MeshPeerDirectoryStatus::success();
        }

        if (meshPeerIsReticulumProtocol(protocol) &&
            record.identity.kind == MeshPeerIdentityKind::ReticulumDestination &&
            record.identity.reticulum.valid &&
            reticulumNodeIdFromDestinationHash(
                record.identity.reticulum.destination_hash) == node_id)
        {
            out_record = record;
            return MeshPeerDirectoryStatus::success();
        }
    }

    return MeshPeerDirectoryStatus::fail(MeshPeerDirectoryStatusCode::NotFound);
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::loadRecent(
    MeshProtocol protocol,
    MeshPeerRecord* out_records,
    std::size_t max_records,
    std::size_t* out_count)
{
    if (!out_count || (!out_records && max_records > 0))
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::InvalidArgument);
    }

    std::vector<const MeshPeerRecord*> matches;
    for (const auto& record : records_)
    {
        if (meshPeerSameProtocol(record.identity.protocol, protocol))
        {
            matches.push_back(&record);
        }
    }
    std::sort(matches.begin(),
              matches.end(),
              [](const MeshPeerRecord* lhs, const MeshPeerRecord* rhs)
              {
                  return lhs->last_seen_s > rhs->last_seen_s;
              });

    const std::size_t count_to_copy = std::min(max_records, matches.size());
    for (std::size_t index = 0; index < count_to_copy; ++index)
    {
        out_records[index] = *matches[index];
    }
    *out_count = count_to_copy;
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::search(
    MeshProtocol protocol,
    const char* query,
    MeshPeerRecord* out_records,
    std::size_t max_records,
    std::size_t* out_count)
{
    if (!query || !out_count || (!out_records && max_records > 0))
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::InvalidArgument);
    }

    std::vector<const MeshPeerRecord*> matches;
    matches.reserve(records_.size());
    for (const auto& record : records_)
    {
        if (!meshPeerSameProtocol(record.identity.protocol, protocol) ||
            !textContains(record.display_name, query))
        {
            continue;
        }
        matches.push_back(&record);
    }
    std::sort(matches.begin(),
              matches.end(),
              [](const MeshPeerRecord* lhs, const MeshPeerRecord* rhs)
              {
                  return lhs->last_seen_s > rhs->last_seen_s;
              });
    *out_count = std::min(max_records, matches.size());
    for (std::size_t index = 0; index < *out_count; ++index)
    {
        out_records[index] = *matches[index];
    }
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::visit(
    MeshProtocol protocol,
    MeshPeerDirectoryView view,
    IMeshPeerDirectoryVisitor& visitor)
{
    for (const MeshPeerRecord& record : records_)
    {
        if (meshPeerSameProtocol(record.identity.protocol, protocol) &&
            matchesDirectoryView(record, view) && !visitor.visit(record))
        {
            break;
        }
    }
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::setUserAlias(
    const MeshPeerIdentity& identity,
    const char* alias)
{
    if (!alias || std::strlen(alias) > kMeshPeerUserAliasMaxLen ||
        !meshPeerIsStableContactIdentity(identity))
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::InvalidArgument);
    }
    const std::size_t index = findIndex(identity);
    if (index >= records_.size())
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::NotFound);
    }
    copyMeshPeerText(records_[index].user_alias,
                     sizeof(records_[index].user_alias),
                     alias);
    records_[index].flags.favorite = alias[0] != '\0';
    markDirty();
    maybeSave();
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::setUserFlags(
    const MeshPeerIdentity& identity,
    const MeshPeerUserFlags& flags)
{
    if (!meshPeerIsStableContactIdentity(identity))
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::Unsupported);
    }
    const std::size_t index = findIndex(identity);
    if (index >= records_.size())
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::NotFound);
    }
    records_[index].flags = flags;
    markDirty();
    maybeSave();
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::setKeyManuallyVerified(
    const MeshPeerIdentity& identity,
    bool verified)
{
    const std::size_t index = findIndex(identity);
    if (index >= records_.size())
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::NotFound);
    }
    MeshPeerRecord& record = records_[index];
    if (record.identity.protocol == MeshProtocol::Meshtastic &&
        record.meshtastic.has_public_key)
    {
        record.meshtastic.key_manually_verified = verified;
    }
    else if (record.identity.protocol == MeshProtocol::MeshCore &&
             (record.meshcore.has_public_key ||
              record.identity.kind == MeshPeerIdentityKind::PublicKey))
    {
        record.meshcore.public_key_verified = verified;
    }
    else
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::Unsupported);
    }
    markDirty();
    maybeSave();
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::remove(
    const MeshPeerIdentity& identity)
{
    const std::size_t index = findIndex(identity);
    if (index >= records_.size())
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::NotFound);
    }
    records_.erase(records_.begin() + static_cast<std::ptrdiff_t>(index));
    markDirty();
    maybeSave();
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::clearProtocol(MeshProtocol protocol)
{
    const auto old_size = records_.size();
    records_.erase(std::remove_if(records_.begin(),
                                  records_.end(),
                                  [protocol](const MeshPeerRecord& record)
                                  {
                                      return meshPeerSameProtocol(
                                          record.identity.protocol,
                                          protocol);
                                  }),
                   records_.end());
    if (records_.size() != old_size)
    {
        markDirty();
        maybeSave();
    }
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryCapacity MeshPeerDirectoryCore::capacityFor(
    MeshProtocol protocol) const
{
    if (protocol == MeshProtocol::Meshtastic)
    {
        return options_.meshtastic_capacity;
    }
    if (protocol == MeshProtocol::MeshCore)
    {
        return options_.meshcore_capacity;
    }
    if (meshPeerIsReticulumProtocol(protocol))
    {
        return options_.reticulum_capacity;
    }
    return {};
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::flush()
{
    if (!dirty_)
    {
        return MeshPeerDirectoryStatus::success();
    }
    return saveRecords();
}

bool MeshPeerDirectoryCore::persistencePending() const
{
    return dirty_;
}

uint32_t MeshPeerDirectoryCore::persistenceRevision() const
{
    return persistence_revision_;
}

std::size_t MeshPeerDirectoryCore::persistenceSnapshotSize() const
{
    return sizeof(PersistedMeshPeerDirectoryHeaderV1) +
           records_.size() * sizeof(PersistedMeshPeerEntryV2);
}

bool MeshPeerDirectoryCore::encodePersistenceSnapshot(
    uint8_t* out,
    std::size_t out_len,
    uint32_t* out_revision) const
{
    if (!out || !out_revision || out_len < persistenceSnapshotSize())
    {
        return false;
    }

    const std::size_t entries_len =
        records_.size() * sizeof(PersistedMeshPeerEntryV2);
    auto* entries_data =
        out + sizeof(PersistedMeshPeerDirectoryHeaderV1);
    for (std::size_t index = 0; index < records_.size(); ++index)
    {
        PersistedMeshPeerEntryV2 persisted{};
        copyIntoPersisted(persisted, records_[index]);
        std::memcpy(entries_data + index * sizeof(PersistedMeshPeerEntryV2),
                    &persisted,
                    sizeof(persisted));
    }

    PersistedMeshPeerDirectoryHeaderV1 header{};
    header.count = static_cast<uint32_t>(records_.size());
    header.crc = computeBlobCrc(entries_data, entries_len);
    std::memcpy(out, &header, sizeof(header));
    *out_revision = persistence_revision_;
    return true;
}

bool MeshPeerDirectoryCore::persistEncodedSnapshot(const uint8_t* data,
                                                   std::size_t len,
                                                   uint32_t revision)
{
    if (!data || len == 0 || revision != persistence_revision_)
    {
        return false;
    }
    if (!blob_store_.saveBlob(data, len))
    {
        return false;
    }
    (void)acknowledgePersistedRevision(revision);
    return true;
}
bool MeshPeerDirectoryCore::acknowledgePersistedRevision(uint32_t revision)
{
    if (revision != persistence_revision_) return false;
    dirty_ = false;
    return true;
}

std::size_t MeshPeerDirectoryCore::count(MeshProtocol protocol) const
{
    return countForProtocol(protocol);
}

void MeshPeerDirectoryCore::clear()
{
    records_.clear();
    markDirty();
    blob_store_.clearBlob();
    dirty_ = false;
}

uint32_t MeshPeerDirectoryCore::computeBlobCrc(const uint8_t* data,
                                               std::size_t len)
{
    uint32_t hash = 2166136261UL;
    for (std::size_t index = 0; index < len; ++index)
    {
        hash ^= data ? data[index] : 0U;
        hash *= 16777619UL;
    }
    return hash;
}

bool MeshPeerDirectoryCore::decodeBlob(std::vector<MeshPeerRecord>& out,
                                       const uint8_t* data,
                                       std::size_t len)
{
    out.clear();
    if (!data || len < sizeof(PersistedMeshPeerDirectoryHeaderV1))
    {
        return false;
    }

    PersistedMeshPeerDirectoryHeaderV1 header{};
    std::memcpy(&header, data, sizeof(header));
    if (header.magic != kMeshPeerDirectoryMagic ||
        (header.version != kMeshPeerDirectoryPersistVersionV1 &&
         header.version != kMeshPeerDirectoryPersistVersion))
    {
        return false;
    }

    const std::size_t entries_len = len - sizeof(header);
    const std::size_t entry_size =
        header.version == kMeshPeerDirectoryPersistVersionV1
            ? sizeof(PersistedMeshPeerEntryV1)
            : sizeof(PersistedMeshPeerEntryV2);
    if ((entries_len % entry_size) != 0 ||
        (entries_len / entry_size) != header.count)
    {
        return false;
    }

    const uint8_t* entries_data = data + sizeof(header);
    if (computeBlobCrc(entries_data, entries_len) != header.crc)
    {
        return false;
    }

    out.reserve(header.count);
    for (std::size_t index = 0; index < header.count; ++index)
    {
        MeshPeerRecord record{};
        bool valid = false;
        if (header.version == kMeshPeerDirectoryPersistVersionV1)
        {
            PersistedMeshPeerEntryV1 persisted{};
            std::memcpy(&persisted,
                        entries_data + index * entry_size,
                        sizeof(persisted));
            valid = copyFromPersisted(record, persisted);
        }
        else
        {
            PersistedMeshPeerEntryV2 persisted{};
            std::memcpy(&persisted,
                        entries_data + index * entry_size,
                        sizeof(persisted));
            valid = copyFromPersisted(record, persisted);
        }
        if (valid)
        {
            out.push_back(record);
        }
    }
    return true;
}

void MeshPeerDirectoryCore::encodeBlob(
    std::vector<uint8_t>& out,
    const std::vector<MeshPeerRecord>& records)
{
    const std::size_t entries_len =
        records.size() * sizeof(PersistedMeshPeerEntryV2);
    out.assign(sizeof(PersistedMeshPeerDirectoryHeaderV1) + entries_len, 0);

    auto* entries_data = out.data() + sizeof(PersistedMeshPeerDirectoryHeaderV1);
    for (std::size_t index = 0; index < records.size(); ++index)
    {
        PersistedMeshPeerEntryV2 persisted{};
        copyIntoPersisted(persisted, records[index]);
        std::memcpy(entries_data + index * sizeof(PersistedMeshPeerEntryV2),
                    &persisted,
                    sizeof(persisted));
    }

    PersistedMeshPeerDirectoryHeaderV1 header{};
    header.count = static_cast<uint32_t>(records.size());
    header.crc = computeBlobCrc(entries_data, entries_len);
    std::memcpy(out.data(), &header, sizeof(header));
}

std::size_t MeshPeerDirectoryCore::findIndex(
    const MeshPeerIdentity& identity) const
{
    for (std::size_t index = 0; index < records_.size(); ++index)
    {
        if (sameMeshPeerIdentity(records_[index].identity, identity))
        {
            return index;
        }
    }
    return records_.size();
}

std::size_t MeshPeerDirectoryCore::countForProtocol(MeshProtocol protocol) const
{
    std::size_t result = 0;
    for (const auto& record : records_)
    {
        if (meshPeerSameProtocol(record.identity.protocol, protocol))
        {
            ++result;
        }
    }
    return result;
}

void MeshPeerDirectoryCore::evictOldest(MeshProtocol protocol)
{
    auto oldest = records_.end();
    for (auto it = records_.begin(); it != records_.end(); ++it)
    {
        if (!meshPeerSameProtocol(it->identity.protocol, protocol))
        {
            continue;
        }
        if (oldest == records_.end() || it->last_seen_s < oldest->last_seen_s)
        {
            oldest = it;
        }
    }
    if (oldest != records_.end())
    {
        records_.erase(oldest);
    }
}

void MeshPeerDirectoryCore::markDirty()
{
    dirty_ = true;
    ++persistence_revision_;
    if (persistence_revision_ == 0)
    {
        persistence_revision_ = 1;
    }
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::saveRecords()
{
    std::vector<uint8_t> blob;
    encodeBlob(blob, records_);
    const uint32_t revision = persistence_revision_;
    if (!blob_store_.saveBlob(blob.empty() ? nullptr : blob.data(), blob.size()))
    {
        return MeshPeerDirectoryStatus::fail(MeshPeerDirectoryStatusCode::IoError);
    }
    if (revision == persistence_revision_)
    {
        dirty_ = false;
    }
    return MeshPeerDirectoryStatus::success();
}

void MeshPeerDirectoryCore::maybeSave()
{
    if (options_.auto_save)
    {
        (void)saveRecords();
    }
}

} // namespace chat

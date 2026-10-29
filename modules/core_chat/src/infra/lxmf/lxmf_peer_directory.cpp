/**
 * @file lxmf_peer_directory.cpp
 * @brief Reticulum peer directory application service.
 */

#include "chat/infra/lxmf/lxmf_peer_directory.h"

#include <cstring>

namespace chat::lxmf::runtime
{
namespace
{

bool isZeroBytes(const uint8_t* data, std::size_t len)
{
    if (!data || len == 0)
    {
        return true;
    }
    for (std::size_t index = 0; index < len; ++index)
    {
        if (data[index] != 0)
        {
            return false;
        }
    }
    return true;
}

bool hasDirectoryKeys(const PeerInfo& peer)
{
    return !isZeroBytes(peer.destination_hash, sizeof(peer.destination_hash)) &&
           !isZeroBytes(peer.identity_hash, sizeof(peer.identity_hash)) &&
           !isZeroBytes(peer.enc_pub, sizeof(peer.enc_pub)) &&
           !isZeroBytes(peer.sig_pub, sizeof(peer.sig_pub));
}

bool hasUsableRatchet(const PeerInfo& peer)
{
    return peer.has_ratchet &&
           !isZeroBytes(peer.ratchet_pub, sizeof(peer.ratchet_pub));
}

MeshPeerRecord makeRecordForPeer(const PeerInfo& peer,
                                 MeshPeerSource source,
                                 uint32_t now_s)
{
    MeshPeerRecord record{};
    record.valid = true;
    record.identity = makeMeshPeerReticulumIdentity(reticulumIdentityForPeer(peer));
    record.source = source;
    record.first_seen_s = peer.last_seen_s != 0 ? peer.last_seen_s : now_s;
    record.last_seen_s = peer.last_seen_s != 0 ? peer.last_seen_s : now_s;
    copyMeshPeerText(record.display_name,
                     sizeof(record.display_name),
                     peer.display_name);
    record.reticulum.identity = record.identity.reticulum;
    record.reticulum.has_public_keys = true;
    std::memcpy(record.reticulum.enc_pub,
                peer.enc_pub,
                sizeof(record.reticulum.enc_pub));
    std::memcpy(record.reticulum.sig_pub,
                peer.sig_pub,
                sizeof(record.reticulum.sig_pub));
    if (hasUsableRatchet(peer))
    {
        record.reticulum.has_ratchet = true;
        std::memcpy(record.reticulum.ratchet_pub,
                    peer.ratchet_pub,
                    sizeof(record.reticulum.ratchet_pub));
        record.reticulum.ratchet_seen_s = peer.ratchet_seen_s;
    }
    record.reticulum.delivery = true;
    return record;
}

} // namespace

ReticulumPeerIdentity reticulumIdentityForPeer(const PeerInfo& peer)
{
    return makeReticulumPeerIdentity(peer.destination_hash, peer.identity_hash);
}

PeerDirectoryService::PeerDirectoryService(IMeshPeerDirectory* directory)
    : directory_(directory)
{
}

void PeerDirectoryService::setDirectory(IMeshPeerDirectory* directory)
{
    directory_ = directory;
}

bool PeerDirectoryService::hasDirectory() const
{
    return directory_ != nullptr;
}

PeerInfo* PeerDirectoryService::applyRecord(DestinationRegistry& registry,
                                            const MeshPeerRecord& record,
                                            uint32_t now_s) const
{
    if (!meshPeerRecordIsValid(record) || record.flags.ignored ||
        !meshPeerSameProtocol(record.identity.protocol, MeshProtocol::Reticulum) ||
        record.identity.kind != MeshPeerIdentityKind::ReticulumDestination ||
        !record.reticulum.has_public_keys)
    {
        return nullptr;
    }

    const ReticulumPeerIdentity& identity = record.reticulum.identity.valid
                                                ? record.reticulum.identity
                                                : record.identity.reticulum;
    if (!identity.valid ||
        isZeroBytes(identity.destination_hash, sizeof(identity.destination_hash)) ||
        isZeroBytes(identity.identity_hash, sizeof(identity.identity_hash)) ||
        isZeroBytes(record.reticulum.enc_pub, sizeof(record.reticulum.enc_pub)) ||
        isZeroBytes(record.reticulum.sig_pub, sizeof(record.reticulum.sig_pub)))
    {
        return nullptr;
    }

    PeerInfo& peer = registry.upsertDestination(identity.destination_hash);
    std::memcpy(peer.identity_hash, identity.identity_hash, sizeof(peer.identity_hash));
    std::memcpy(peer.enc_pub, record.reticulum.enc_pub, sizeof(peer.enc_pub));
    std::memcpy(peer.sig_pub, record.reticulum.sig_pub, sizeof(peer.sig_pub));
    if (record.reticulum.has_ratchet &&
        !isZeroBytes(record.reticulum.ratchet_pub,
                     sizeof(record.reticulum.ratchet_pub)))
    {
        std::memcpy(peer.ratchet_pub,
                    record.reticulum.ratchet_pub,
                    sizeof(peer.ratchet_pub));
        peer.has_ratchet = true;
        peer.ratchet_seen_s = record.reticulum.ratchet_seen_s;
    }
    else
    {
        std::memset(peer.ratchet_pub, 0, sizeof(peer.ratchet_pub));
        peer.has_ratchet = false;
        peer.ratchet_seen_s = 0;
    }
    peer.last_seen_s = record.last_seen_s != 0 ? record.last_seen_s : now_s;
    peer.last_path_request_ms = 0;
    copyMeshPeerText(peer.display_name,
                     sizeof(peer.display_name),
                     record.display_name);
    return &peer;
}

PeerDirectoryLoadResult PeerDirectoryService::findOrLoadByNodeId(
    DestinationRegistry& registry,
    NodeId node_id,
    uint32_t now_s) const
{
    PeerDirectoryLoadResult result{};
    if (node_id == 0)
    {
        result.status =
            MeshPeerDirectoryStatus::fail(MeshPeerDirectoryStatusCode::InvalidArgument);
        return result;
    }

    if (PeerInfo* peer = registry.findByNodeId(node_id))
    {
        result.peer = peer;
        return result;
    }

    if (!directory_)
    {
        result.status =
            MeshPeerDirectoryStatus::fail(MeshPeerDirectoryStatusCode::StorageUnavailable);
        return result;
    }

    MeshPeerRecord record{};
    result.status =
        directory_->findByNodeId(MeshProtocol::Reticulum, node_id, record);
    if (!result.status.succeeded())
    {
        return result;
    }

    result.peer = applyRecord(registry, record, now_s);
    result.loaded_from_directory = result.peer != nullptr;
    if (!result.peer)
    {
        result.status =
            MeshPeerDirectoryStatus::fail(MeshPeerDirectoryStatusCode::InvalidArgument);
    }
    return result;
}

PeerDirectoryLoadResult PeerDirectoryService::findOrLoadByDestinationHash(
    DestinationRegistry& registry,
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    uint32_t now_s) const
{
    PeerDirectoryLoadResult result{};
    if (!destination_hash)
    {
        result.status =
            MeshPeerDirectoryStatus::fail(MeshPeerDirectoryStatusCode::InvalidArgument);
        return result;
    }

    if (PeerInfo* peer = registry.findByDestinationHash(destination_hash))
    {
        result.peer = peer;
        return result;
    }

    if (!directory_)
    {
        result.status =
            MeshPeerDirectoryStatus::fail(MeshPeerDirectoryStatusCode::StorageUnavailable);
        return result;
    }

    const MeshPeerIdentity identity =
        makeMeshPeerReticulumIdentity(makeReticulumDestinationIdentity(destination_hash));
    MeshPeerRecord record{};
    result.status = directory_->find(identity, record);
    if (!result.status.succeeded())
    {
        return result;
    }

    result.peer = applyRecord(registry, record, now_s);
    result.loaded_from_directory = result.peer != nullptr;
    if (!result.peer)
    {
        result.status =
            MeshPeerDirectoryStatus::fail(MeshPeerDirectoryStatusCode::InvalidArgument);
    }
    return result;
}

MeshActionResult PeerDirectoryService::persistPeerAddressNow(
    const PeerInfo& peer,
    bool favorite,
    uint32_t now_s) const
{
    if (!hasDirectoryKeys(peer))
    {
        return MeshActionResult::fail(MeshOperationFailure::PeerKeyMissing);
    }
    if (!directory_)
    {
        return MeshActionResult::fail(MeshOperationFailure::NotReady);
    }

    const PeerDirectoryWriteResult write =
        recordPeer(peer, MeshPeerSource::Manual, true, favorite, now_s);
    if (!write.succeeded())
    {
        return MeshActionResult::fail(MeshOperationFailure::Unknown);
    }
    return MeshActionResult::success();
}

PeerDirectoryWriteResult PeerDirectoryService::recordPeer(const PeerInfo& peer,
                                                          MeshPeerSource source,
                                                          bool update_favorite,
                                                          bool favorite,
                                                          uint32_t now_s) const
{
    PeerDirectoryWriteResult result{};
    if (!directory_)
    {
        result.record_status =
            MeshPeerDirectoryStatus::fail(MeshPeerDirectoryStatusCode::StorageUnavailable);
        return result;
    }
    if (!hasDirectoryKeys(peer))
    {
        result.record_status =
            MeshPeerDirectoryStatus::fail(MeshPeerDirectoryStatusCode::InvalidArgument);
        return result;
    }

    MeshPeerRecord record = makeRecordForPeer(peer, source, now_s);

    MeshPeerUserFlags flags{};
    if (update_favorite)
    {
        MeshPeerRecord existing{};
        if (directory_->find(record.identity, existing).succeeded())
        {
            flags = existing.flags;
        }
        flags.favorite = favorite;
    }

    result.record_status = directory_->record(record);
    if (!result.record_status.succeeded())
    {
        return result;
    }

    if (update_favorite)
    {
        result.flags_attempted = true;
        result.flags_status = directory_->setUserFlags(record.identity, flags);
    }
    return result;
}

PeerDirectoryLoadRecentResult PeerDirectoryService::loadRecent(
    DestinationRegistry& registry,
    MeshPeerRecord* scratch,
    std::size_t scratch_count,
    NodeId* out_loaded_nodes,
    std::size_t max_loaded_nodes,
    uint32_t now_s) const
{
    PeerDirectoryLoadRecentResult result{};
    if (!directory_)
    {
        result.status =
            MeshPeerDirectoryStatus::fail(MeshPeerDirectoryStatusCode::StorageUnavailable);
        return result;
    }
    if (!scratch || scratch_count == 0)
    {
        result.status =
            MeshPeerDirectoryStatus::fail(MeshPeerDirectoryStatusCode::InvalidArgument);
        return result;
    }

    std::size_t count = 0;
    result.status =
        directory_->loadRecent(MeshProtocol::Reticulum, scratch, scratch_count, &count);
    if (!result.status.succeeded())
    {
        return result;
    }

    result.scanned = count;
    for (std::size_t index = 0; index < count; ++index)
    {
        PeerInfo* peer = applyRecord(registry, scratch[index], now_s);
        if (!peer)
        {
            continue;
        }
        if (out_loaded_nodes && result.loaded < max_loaded_nodes)
        {
            out_loaded_nodes[result.loaded] = peer->node_id;
        }
        ++result.loaded;
    }
    return result;
}

PeerDirectoryLoadRecentResult PeerDirectoryService::loadRecentAndQueue(
    DestinationRegistry& registry,
    uint32_t now_s)
{
    std::array<NodeId, kHotLoadRecords> loaded_nodes = {};
    PeerDirectoryLoadRecentResult result = loadRecent(registry,
                                                      hot_load_records_.data(),
                                                      hot_load_records_.size(),
                                                      loaded_nodes.data(),
                                                      loaded_nodes.size(),
                                                      now_s);
    if (!result.status.succeeded())
    {
        return result;
    }

    const std::size_t queued_count =
        result.loaded < loaded_nodes.size() ? result.loaded : loaded_nodes.size();
    for (std::size_t index = 0; index < queued_count; ++index)
    {
        const PeerInfo* peer = registry.findByNodeId(loaded_nodes[index]);
        if (peer)
        {
            queuePeerUpdate(*peer);
        }
    }
    return result;
}

void PeerDirectoryService::queuePeerUpdate(const PeerInfo& peer)
{
    if (peer.node_id == 0)
    {
        return;
    }

    for (std::size_t index = 0; index < pending_projection_count_; ++index)
    {
        if (pending_projection_nodes_[index] == peer.node_id)
        {
            return;
        }
    }

    if (pending_projection_count_ >= pending_projection_nodes_.size())
    {
        return;
    }

    pending_projection_nodes_[pending_projection_count_] = peer.node_id;
    ++pending_projection_count_;
}

void PeerDirectoryService::pumpQueuedPeerUpdates(DestinationRegistry& registry,
                                                 IPeerProjectionSink& sink,
                                                 uint32_t now_ms,
                                                 bool maintenance_window,
                                                 uint32_t sleep_interval_ms,
                                                 uint32_t screen_interval_ms)
{
    if (pending_projection_count_ == 0)
    {
        return;
    }

    const uint32_t interval_ms =
        maintenance_window ? sleep_interval_ms : screen_interval_ms;
    if (last_projection_ms_ != 0 &&
        (now_ms - last_projection_ms_) < interval_ms)
    {
        return;
    }

    const NodeId node_id = pending_projection_nodes_[0];
    for (std::size_t index = 1; index < pending_projection_count_; ++index)
    {
        pending_projection_nodes_[index - 1U] = pending_projection_nodes_[index];
    }
    --pending_projection_count_;
    last_projection_ms_ = now_ms;

    const PeerInfo* peer = registry.findByNodeId(node_id);
    if (peer)
    {
        sink.publishPeerUpdate(*peer);
    }
}

} // namespace chat::lxmf::runtime

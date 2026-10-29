/**
 * @file lxmf_propagation_client.cpp
 * @brief Propagation runtime state owner.
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_propagation_client.h"
#include "chat/infra/lxmf/lxmf_propagation_sync.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

namespace chat::lxmf::runtime
{
namespace
{

bool bytesEqual(const uint8_t* a, const uint8_t* b, std::size_t len)
{
    return a && b && std::memcmp(a, b, len) == 0;
}

void copyBytes(uint8_t* out, const uint8_t* in, std::size_t len)
{
    if (out && in && len != 0)
    {
        std::memcpy(out, in, len);
    }
}

} // namespace

PropagationRuntime& PropagationClient::state()
{
    return state_;
}

const PropagationRuntime& PropagationClient::state() const
{
    return state_;
}

PropagationActivePeerSelection PropagationClient::selectActivePeer(
    bool automatic,
    const uint8_t configured_hash[reticulum::kTruncatedHashSize],
    uint32_t now_s,
    uint32_t peer_ttl_s,
    bool sync_on_start)
{
    const PropagationPeerState* selected =
        selectPropagationPeer(state_, automatic, configured_hash, now_s, peer_ttl_s);
    if (!selected)
    {
        clearActivePeer();
        return {};
    }

    const bool changed =
        !state_.has_active_node ||
        !bytesEqual(state_.active_node_hash,
                    selected->propagation_hash,
                    sizeof(state_.active_node_hash));
    copyBytes(state_.active_node_hash,
              selected->propagation_hash,
              sizeof(state_.active_node_hash));
    state_.has_active_node = true;
    if (changed)
    {
        state_.initial_sync_pending = sync_on_start;
        if (!sync_on_start)
        {
            state_.last_sync_s = now_s;
        }
        state_.sync_stage = PropagationSyncStage::Idle;
        state_.sync_wants.clear();
        state_.sync_haves.clear();
        clearPropagationDeliveryCommits(state_);
        state_.persistence_started_ms = 0;
        std::memset(state_.sync_request_id, 0, sizeof(state_.sync_request_id));
    }
    return PropagationActivePeerSelection{selected, changed};
}

void PropagationClient::clearActivePeer()
{
    state_.has_active_node = false;
    std::memset(state_.active_node_hash, 0, sizeof(state_.active_node_hash));
}

bool PropagationClient::canQueueUpload(std::size_t max_pending) const
{
    return state_.pending_uploads.size() < max_pending;
}

PendingPropagationUpload* PropagationClient::queueUpload(
    PendingPropagationUpload upload,
    std::size_t max_pending)
{
    if (!canQueueUpload(max_pending))
    {
        return nullptr;
    }

    state_.pending_uploads.push_back(std::move(upload));
    return &state_.pending_uploads.back();
}

bool PropagationClient::hasPendingUploads() const
{
    return !state_.pending_uploads.empty();
}

PendingPropagationUpload* PropagationClient::firstPendingUpload()
{
    return state_.pending_uploads.empty() ? nullptr
                                          : &state_.pending_uploads.front();
}

const PendingPropagationUpload* PropagationClient::firstPendingUpload() const
{
    return state_.pending_uploads.empty() ? nullptr
                                          : &state_.pending_uploads.front();
}

bool PropagationClient::removeFirstPendingUpload()
{
    if (state_.pending_uploads.empty())
    {
        return false;
    }

    state_.pending_uploads.erase(state_.pending_uploads.begin());
    return true;
}

void PropagationClient::markUploadWaitingForNode(
    PendingPropagationUpload& upload)
{
    upload.state = PropagationUploadState::WaitingNode;
#if !defined(TRAIL_MATE_RETICULUM_PARSE_ONLY)
    stamp_.reset();
#endif
}

bool PropagationClient::bindUploadNode(PendingPropagationUpload& upload,
                                       const PropagationPeerState& node)
{
    const bool node_changed =
        !bytesEqual(upload.node_hash,
                    node.propagation_hash,
                    sizeof(upload.node_hash)) ||
        upload.stamp_cost != node.stamp_cost;
    if (node_changed)
    {
#if !defined(TRAIL_MATE_RETICULUM_PARSE_ONLY)
        stamp_.reset();
#endif
        copyBytes(upload.node_hash,
                  node.propagation_hash,
                  sizeof(upload.node_hash));
        upload.stamp_cost = node.stamp_cost;
    }
    if (node_changed || upload.state == PropagationUploadState::WaitingNode)
    {
        upload.state = PropagationUploadState::NeedsStamp;
    }
    return node_changed;
}

bool PropagationClient::beginUploadStamp(PendingPropagationUpload& upload)
{
    if (upload.state != PropagationUploadState::NeedsStamp)
    {
        return false;
    }
#if !defined(TRAIL_MATE_RETICULUM_PARSE_ONLY)
    if (stamp_.begin(upload.transient_id, upload.stamp_cost))
    {
        upload.state = PropagationUploadState::Stamping;
        return true;
    }
#endif
    upload.state = PropagationUploadState::Failed;
    return false;
}

bool PropagationClient::completeUploadStamp(
    PendingPropagationUpload& upload,
    const uint8_t stamp[reticulum::kFullHashSize])
{
    if (!stamp || upload.state != PropagationUploadState::Stamping)
    {
        upload.state = PropagationUploadState::Failed;
        return false;
    }
    upload.transient_data.insert(upload.transient_data.end(),
                                 stamp,
                                 stamp + reticulum::kFullHashSize);
    upload.state = PropagationUploadState::Ready;
    return true;
}

void PropagationClient::markUploadFailed(PendingPropagationUpload& upload)
{
    upload.state = PropagationUploadState::Failed;
}

void PropagationClient::markUploadQueuedToLink(
    PendingPropagationUpload& upload)
{
    upload.state = PropagationUploadState::QueuedToLink;
}

void PropagationClient::markExpiredUploads(uint32_t now_ms, uint32_t ttl_ms)
{
    for (auto& upload : state_.pending_uploads)
    {
        if (upload.state != PropagationUploadState::Failed &&
            upload.created_ms != 0 && (now_ms - upload.created_ms) > ttl_ms)
        {
            upload.state = PropagationUploadState::Failed;
        }
    }
}

PendingPropagationUploadList PropagationClient::takeFailedUploads()
{
    PendingPropagationUploadList failed;
    auto& uploads = state_.pending_uploads;
    for (auto it = uploads.begin(); it != uploads.end();)
    {
        if (it->state != PropagationUploadState::Failed)
        {
            ++it;
            continue;
        }
        failed.push_back(std::move(*it));
        it = uploads.erase(it);
    }
    return failed;
}

PendingPropagationUploadList PropagationClient::takeAllPendingUploads()
{
    PendingPropagationUploadList uploads;
    uploads.swap(state_.pending_uploads);
    return uploads;
}

void PropagationClient::resetStampingUploads()
{
#if !defined(TRAIL_MATE_RETICULUM_PARSE_ONLY)
    stamp_.reset();
#endif
    for (auto& upload : state_.pending_uploads)
    {
        if (upload.state == PropagationUploadState::Stamping)
        {
            upload.state = PropagationUploadState::NeedsStamp;
        }
    }
}

void PropagationClient::resetForDisabled()
{
    state_.sync_wants.clear();
    state_.sync_haves.clear();
    clearPropagationDeliveryCommits(state_);
    state_.persistence_started_ms = 0;
    state_.sync_started_ms = 0;
    state_.sync_stage = PropagationSyncStage::Idle;
    state_.initial_sync_pending = true;
    clearActivePeer();
    std::memset(state_.sync_request_id, 0, sizeof(state_.sync_request_id));
#if !defined(TRAIL_MATE_RETICULUM_PARSE_ONLY)
    stamp_.reset();
#endif
}

void PropagationClient::resetForNetworkConfig(bool sync_on_start)
{
    resetStampingUploads();
    state_.sync_wants.clear();
    state_.sync_haves.clear();
    clearPropagationDeliveryCommits(state_);
    state_.persistence_started_ms = 0;
    state_.sync_started_ms = 0;
    state_.sync_stage = PropagationSyncStage::Idle;
    state_.initial_sync_pending = sync_on_start;
    clearActivePeer();
    std::memset(state_.sync_request_id, 0, sizeof(state_.sync_request_id));
}

bool PropagationClient::syncDue(uint32_t now_s, uint32_t sync_interval_s) const
{
    return propagationSyncDue(state_, now_s, sync_interval_s);
}

PropagationSyncStage PropagationClient::syncStage() const
{
    return state_.sync_stage;
}

const PropagationIdList& PropagationClient::syncWants() const
{
    return state_.sync_wants;
}

const PropagationIdList& PropagationClient::syncHaves() const
{
    return state_.sync_haves;
}

bool PropagationClient::syncHavesEmpty() const
{
    return state_.sync_haves.empty();
}

std::size_t PropagationClient::pendingDeliveryCount() const
{
    return state_.pending_deliveries.size();
}

bool PropagationClient::startSyncIfDue(uint32_t now_s,
                                       uint32_t now_ms,
                                       uint32_t sync_interval_s)
{
    return startPropagationSyncIfDue(state_, now_s, now_ms, sync_interval_s);
}

void PropagationClient::markSyncRequestSent(
    const uint8_t request_id[reticulum::kTruncatedHashSize],
    PropagationSyncStage next_stage)
{
    markPropagationSyncRequestSent(state_, request_id, next_stage);
}

bool PropagationClient::syncRequestMatches(
    const LinkPendingRequest& request) const
{
    return propagationSyncRequestMatches(state_, request);
}

void PropagationClient::markSyncFailed()
{
    markPropagationSyncFailed(state_);
}

void PropagationClient::noteListingResult(const PropagationIdList& remote_ids,
                                          std::size_t max_messages)
{
    notePropagationListingResult(state_, remote_ids, max_messages);
}

bool PropagationClient::registerDeliveryCommit(
    const uint8_t transient_id[reticulum::kFullHashSize],
    const uint8_t message_hash[reticulum::kFullHashSize],
    std::size_t max_pending)
{
    return awaitPropagationDeliveryCommit(state_,
                                          transient_id,
                                          message_hash,
                                          std::max<std::size_t>(1U, max_pending));
}

void PropagationClient::rememberDeliveredTransient(
    const uint8_t transient_id[reticulum::kFullHashSize],
    uint32_t now_s,
    std::size_t max_transients)
{
    rememberDeliveredPropagationTransient(state_, transient_id, now_s, max_transients);
}

void PropagationClient::noteDownloadResult(bool registration_failed,
                                           uint32_t now_ms)
{
    notePropagationDownloadResult(state_, registration_failed, now_ms);
}

bool PropagationClient::pollPersistence(uint32_t now_ms, uint32_t ttl_ms)
{
    return pollPropagationPersistence(state_, now_ms, ttl_ms);
}

bool PropagationClient::noteDeliveryCommit(
    const uint8_t message_hash[reticulum::kFullHashSize],
    bool accepted,
    uint32_t now_s,
    std::size_t max_transients)
{
    return notePropagationDeliveryCommit(state_, message_hash, accepted, now_s, max_transients);
}

void PropagationClient::markAcknowledged()
{
    markPropagationAcknowledged(state_);
}

std::size_t PropagationClient::syncHaveCount() const
{
    return state_.sync_haves.size();
}

void PropagationClient::finishSyncComplete(uint32_t now_s)
{
    finishPropagationSyncComplete(state_, now_s);
}

void PropagationClient::finishSyncFailed(uint32_t now_s)
{
    finishPropagationSyncFailed(state_, now_s);
}

void PropagationClient::cull(uint32_t now_s,
                             const PropagationRuntimeLimits& limits)
{
    cullPropagationRuntime(state_, now_s, limits);
}

const PropagationPeerState* PropagationClient::notePeerAnnounce(
    const uint8_t propagation_hash[reticulum::kTruncatedHashSize],
    const uint8_t delivery_hash[reticulum::kTruncatedHashSize],
    const uint8_t identity_hash[reticulum::kTruncatedHashSize],
    uint8_t hops,
    const DecodedPropagationAnnounce& announce_data,
    const uint8_t* public_key,
    uint32_t now_s,
    std::size_t max_peers)
{
    PropagationPeerState& peer = upsertPropagationPeer(state_,
                                                       propagation_hash,
                                                       delivery_hash,
                                                       identity_hash,
                                                       max_peers);
    peer.node_active = announce_data.valid;
    peer.hops = hops;
    if (announce_data.valid)
    {
        peer.announce_timebase_s = announce_data.timebase_s;
        peer.transfer_limit_kb = announce_data.transfer_limit_kb;
        peer.sync_limit_kb = announce_data.sync_limit_kb;
        peer.stamp_cost = announce_data.stamp_cost;
        peer.stamp_cost_flexibility = announce_data.stamp_cost_flexibility;
        peer.peering_cost = announce_data.peering_cost;
        if (public_key)
        {
            std::memcpy(peer.enc_pub, public_key, sizeof(peer.enc_pub));
            std::memcpy(peer.sig_pub,
                        public_key + sizeof(peer.enc_pub),
                        sizeof(peer.sig_pub));
        }
        std::snprintf(peer.display_name,
                      sizeof(peer.display_name),
                      "%s",
                      announce_data.display_name.c_str());
    }
    markPropagationPeerSeen(peer, now_s);
    return &peer;
}

bool PropagationClient::planBatchAcceptance(
    const uint8_t* plaintext,
    std::size_t plaintext_len,
    const PropagationBatchContext& context,
    const PropagationBatchLimits& limits,
    PropagationBatchAcceptance* out_acceptance)
{
    return planPropagationBatchAcceptance(state_,
                                          plaintext,
                                          plaintext_len,
                                          context,
                                          limits,
                                          out_acceptance);
}

void PropagationClient::noteLocalDeliveryResult(
    const uint8_t transient_id[reticulum::kFullHashSize],
    bool delivered,
    uint32_t now_s,
    std::size_t max_transients)
{
    notePropagationLocalDeliveryResult(state_,
                                       transient_id,
                                       delivered,
                                       now_s,
                                       max_transients);
}

void PropagationClient::noteBatchHandled(
    const PropagationBatchAcceptance& acceptance)
{
    notePropagationBatchMessageHandled(state_, acceptance);
}

bool PropagationClient::planServiceResponse(
    const DecodedLinkRequest& request,
    const PropagationServicePeerContext& peer_context,
    uint32_t now_s,
    const PropagationServiceLimits& limits,
    PropagationServiceResponse* out_response)
{
    return planPropagationServiceResponse(state_,
                                          request,
                                          peer_context,
                                          now_s,
                                          limits,
                                          out_response);
}

#if !defined(TRAIL_MATE_RETICULUM_PARSE_ONLY)
PropagationStampRuntime& PropagationClient::stamp()
{
    return stamp_;
}

const PropagationStampRuntime& PropagationClient::stamp() const
{
    return stamp_;
}
#endif

PeerInfo& PropagationClient::peerScratch()
{
    return peer_scratch_;
}

const PeerInfo& PropagationClient::peerScratch() const
{
    return peer_scratch_;
}

} // namespace chat::lxmf::runtime

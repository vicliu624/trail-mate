/**
 * @file lxmf_propagation_client.h
 * @brief Propagation runtime state owner.
 */

#pragma once
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_identity.h"

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_propagation_runtime.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_propagation_service_runtime.h"
#if !defined(TRAIL_MATE_RETICULUM_PARSE_ONLY)
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_propagation_stamp_runtime.h"
#endif

#include <cstddef>
#include <cstdint>
#include <vector>

namespace chat::lxmf::runtime
{

struct PropagationActivePeerSelection
{
    const PropagationPeerState* peer = nullptr;
    bool changed = false;
};

class PropagationClient
{
  public:
    PropagationClient() = default;
    PropagationClient(const PropagationClient&) = delete;
    PropagationClient& operator=(const PropagationClient&) = delete;
    PropagationClient(PropagationClient&&) = delete;
    PropagationClient& operator=(PropagationClient&&) = delete;

    PropagationRuntime& state();
    const PropagationRuntime& state() const;

    PropagationActivePeerSelection selectActivePeer(
        bool automatic,
        const uint8_t configured_hash[reticulum::kTruncatedHashSize],
        uint32_t now_s,
        uint32_t peer_ttl_s,
        bool sync_on_start);
    void clearActivePeer();

    bool canQueueUpload(std::size_t max_pending) const;
    PendingPropagationUpload* queueUpload(PendingPropagationUpload upload,
                                          std::size_t max_pending);
    bool hasPendingUploads() const;
    PendingPropagationUpload* firstPendingUpload();
    const PendingPropagationUpload* firstPendingUpload() const;
    bool removeFirstPendingUpload();
    void markUploadWaitingForNode(PendingPropagationUpload& upload);
    bool bindUploadNode(PendingPropagationUpload& upload,
                        const PropagationPeerState& node);
    bool beginUploadStamp(PendingPropagationUpload& upload);
    bool completeUploadStamp(
        PendingPropagationUpload& upload,
        const uint8_t stamp[reticulum::kFullHashSize]);
    void markUploadFailed(PendingPropagationUpload& upload);
    void markUploadQueuedToLink(PendingPropagationUpload& upload);
    void markExpiredUploads(uint32_t now_ms, uint32_t ttl_ms);
    PendingPropagationUploadList takeFailedUploads();
    PendingPropagationUploadList takeAllPendingUploads();
    void resetStampingUploads();

    void resetForDisabled();
    void resetForNetworkConfig(bool sync_on_start);
    bool syncDue(uint32_t now_s, uint32_t sync_interval_s) const;
    PropagationSyncStage syncStage() const;
    const PropagationIdList& syncWants() const;
    const PropagationIdList& syncHaves() const;
    bool syncHavesEmpty() const;
    std::size_t pendingDeliveryCount() const;
    bool startSyncIfDue(uint32_t now_s,
                        uint32_t now_ms,
                        uint32_t sync_interval_s);
    void markSyncRequestSent(
        const uint8_t request_id[reticulum::kTruncatedHashSize],
        PropagationSyncStage next_stage);
    bool syncRequestMatches(const LinkPendingRequest& request) const;
    void markSyncFailed();
    void noteListingResult(const PropagationIdList& remote_ids,
                           std::size_t max_messages);
    bool registerDeliveryCommit(
        const uint8_t transient_id[reticulum::kFullHashSize],
        const uint8_t message_hash[reticulum::kFullHashSize],
        std::size_t max_pending);
    void rememberDeliveredTransient(
        const uint8_t transient_id[reticulum::kFullHashSize],
        uint32_t now_s,
        std::size_t max_transients);
    void noteDownloadResult(bool registration_failed, uint32_t now_ms);
    bool pollPersistence(uint32_t now_ms, uint32_t ttl_ms);
    bool noteDeliveryCommit(const uint8_t message_hash[reticulum::kFullHashSize],
                            bool accepted,
                            uint32_t now_s,
                            std::size_t max_transients);
    void markAcknowledged();
    std::size_t syncHaveCount() const;
    void finishSyncComplete(uint32_t now_s);
    void finishSyncFailed(uint32_t now_s);
    void cull(uint32_t now_s, const PropagationRuntimeLimits& limits);
    const PropagationPeerState* notePeerAnnounce(
        const uint8_t propagation_hash[reticulum::kTruncatedHashSize],
        const uint8_t delivery_hash[reticulum::kTruncatedHashSize],
        const uint8_t identity_hash[reticulum::kTruncatedHashSize],
        uint8_t hops,
        const DecodedPropagationAnnounce& announce_data,
        const uint8_t* public_key,
        uint32_t now_s,
        std::size_t max_peers);
    bool planBatchAcceptance(
        const uint8_t* plaintext,
        std::size_t plaintext_len,
        const PropagationBatchContext& context,
        const PropagationBatchLimits& limits,
        PropagationBatchAcceptance* out_acceptance);
    void noteLocalDeliveryResult(
        const uint8_t transient_id[reticulum::kFullHashSize],
        bool delivered,
        uint32_t now_s,
        std::size_t max_transients);
    void noteBatchHandled(const PropagationBatchAcceptance& acceptance);
    bool planServiceResponse(const DecodedLinkRequest& request,
                             const PropagationServicePeerContext& peer_context,
                             uint32_t now_s,
                             const PropagationServiceLimits& limits,
                             PropagationServiceResponse* out_response);

#if !defined(TRAIL_MATE_RETICULUM_PARSE_ONLY)
    PropagationStampRuntime& stamp();
    const PropagationStampRuntime& stamp() const;
#endif

    PeerInfo& peerScratch();
    const PeerInfo& peerScratch() const;

  private:
    PropagationRuntime state_;
#if !defined(TRAIL_MATE_RETICULUM_PARSE_ONLY)
    PropagationStampRuntime stamp_;
#endif
    PeerInfo peer_scratch_{};
};

} // namespace chat::lxmf::runtime

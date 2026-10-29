#include "chat/infra/lxmf/lxmf_propagation_sync.h"
#include <algorithm>
#include <cstring>

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

bool propagationSyncDue(const PropagationRuntime& propagation, uint32_t now_s, uint32_t sync_interval_s)
{
    return propagation.initial_sync_pending ||
           (sync_interval_s != 0 &&
            (propagation.last_sync_s == 0 || now_s < propagation.last_sync_s ||
             (now_s - propagation.last_sync_s) >= sync_interval_s));
}

bool startPropagationSyncIfDue(PropagationRuntime& propagation, uint32_t now_s,
                               uint32_t now_ms,
                               uint32_t sync_interval_s)
{
    if (propagation.sync_stage != PropagationSyncStage::Idle ||
        !propagationSyncDue(propagation, now_s, sync_interval_s))
    {
        return false;
    }

    propagation.sync_stage = PropagationSyncStage::NeedList;
    propagation.sync_started_ms = now_ms;
    propagation.persistence_started_ms = 0;
    propagation.sync_wants.clear();
    propagation.sync_haves.clear();
    clearPropagationDeliveryCommits(propagation);
    std::memset(propagation.sync_request_id, 0, sizeof(propagation.sync_request_id));
    return true;
}

void markPropagationSyncRequestSent(PropagationRuntime& propagation,
                                    const uint8_t request_id[reticulum::kTruncatedHashSize],
                                    PropagationSyncStage next_stage)
{
    if (request_id)
    {
        copyBytes(propagation.sync_request_id, request_id, sizeof(propagation.sync_request_id));
    }
    else
    {
        std::memset(propagation.sync_request_id, 0, sizeof(propagation.sync_request_id));
    }
    propagation.sync_stage = next_stage;
}

bool propagationSyncRequestMatches(const PropagationRuntime& propagation,
                                   const LinkPendingRequest& request)
{
    return request.request_id.size() == sizeof(propagation.sync_request_id) &&
           bytesEqual(request.request_id.data(),
                      propagation.sync_request_id,
                      sizeof(propagation.sync_request_id));
}

void markPropagationSyncFailed(PropagationRuntime& propagation)
{
    propagation.sync_stage = PropagationSyncStage::Failed;
}

void notePropagationListingResult(PropagationRuntime& propagation, const PropagationIdList& remote_ids,
                                  std::size_t max_messages)
{
    const std::size_t limit = std::max<std::size_t>(1U, max_messages);
    propagation.sync_wants.clear();
    propagation.sync_haves.clear();
    for (const auto& transient_id : remote_ids)
    {
        if (transient_id.size() != reticulum::kFullHashSize)
        {
            continue;
        }
        if (hasSeenPropagationTransient(propagation, transient_id.data(), nullptr))
        {
            propagation.sync_haves.push_back(transient_id);
        }
        else if (propagation.sync_wants.size() < limit)
        {
            propagation.sync_wants.push_back(transient_id);
        }
    }
    propagation.sync_stage = propagation.sync_wants.empty()
                                 ? PropagationSyncStage::NeedAcknowledge
                                 : PropagationSyncStage::NeedMessages;
}

void rememberDeliveredPropagationTransient(PropagationRuntime& propagation,
                                           const uint8_t transient_id[reticulum::kFullHashSize],
                                           uint32_t now_s,
                                           std::size_t max_transients)
{
    rememberPropagationTransient(propagation, transient_id, true, now_s, max_transients);
    const bool already_have =
        std::any_of(propagation.sync_haves.begin(),
                    propagation.sync_haves.end(),
                    [transient_id](const auto& have)
                    {
                        return have.size() == reticulum::kFullHashSize &&
                               bytesEqual(have.data(),
                                          transient_id,
                                          reticulum::kFullHashSize);
                    });
    if (!already_have)
    {
        propagation.sync_haves.emplace_back(transient_id,
                                            transient_id + reticulum::kFullHashSize);
    }
}

void notePropagationDownloadResult(PropagationRuntime& propagation, bool registration_failed,
                                   uint32_t now_ms)
{
    propagation.sync_stage =
        registration_failed ? PropagationSyncStage::Failed
                            : (propagation.pending_deliveries.empty()
                                   ? PropagationSyncStage::NeedAcknowledge
                                   : PropagationSyncStage::AwaitingPersistence);
    if (propagation.sync_stage == PropagationSyncStage::AwaitingPersistence)
    {
        propagation.persistence_started_ms = now_ms;
    }
}

bool pollPropagationPersistence(PropagationRuntime& propagation, uint32_t now_ms, uint32_t ttl_ms)
{
    if (propagation.sync_stage != PropagationSyncStage::AwaitingPersistence)
    {
        return true;
    }
    if (propagation.persistence_started_ms == 0 ||
        (now_ms - propagation.persistence_started_ms) > ttl_ms)
    {
        propagation.sync_stage = PropagationSyncStage::Failed;
        return true;
    }
    if (propagationDeliveryCommitsResolved(propagation))
    {
        propagation.sync_stage = propagationDeliveryCommitRejected(propagation)
                                     ? PropagationSyncStage::Failed
                                     : PropagationSyncStage::NeedAcknowledge;
        return true;
    }
    return false;
}

bool notePropagationDeliveryCommit(PropagationRuntime& propagation,
                                   const uint8_t message_hash[reticulum::kFullHashSize],
                                   bool accepted,
                                   uint32_t now_s,
                                   std::size_t max_transients)
{
    const bool matched = commitPropagationDelivery(propagation,
                                                   message_hash,
                                                   accepted,
                                                   now_s,
                                                   max_transients);
    if (matched &&
        propagation.sync_stage == PropagationSyncStage::AwaitingPersistence &&
        propagationDeliveryCommitsResolved(propagation))
    {
        propagation.sync_stage = propagationDeliveryCommitRejected(propagation)
                                     ? PropagationSyncStage::Failed
                                     : PropagationSyncStage::NeedAcknowledge;
    }
    return matched;
}

void markPropagationAcknowledged(PropagationRuntime& propagation)
{
    propagation.sync_stage = PropagationSyncStage::Complete;
}

void finishPropagationSyncComplete(PropagationRuntime& propagation, uint32_t now_s)
{
    propagation.last_sync_s = now_s;
    propagation.initial_sync_pending = false;
    propagation.sync_wants.clear();
    propagation.sync_haves.clear();
    clearPropagationDeliveryCommits(propagation);
    propagation.persistence_started_ms = 0;
    propagation.sync_started_ms = 0;
    propagation.sync_stage = PropagationSyncStage::Idle;
    std::memset(propagation.sync_request_id, 0, sizeof(propagation.sync_request_id));
}

void finishPropagationSyncFailed(PropagationRuntime& propagation, uint32_t now_s)
{
    propagation.sync_wants.clear();
    propagation.sync_haves.clear();
    clearPropagationDeliveryCommits(propagation);
    propagation.persistence_started_ms = 0;
    propagation.sync_started_ms = 0;
    propagation.sync_stage = PropagationSyncStage::Idle;
    propagation.initial_sync_pending = false;
    propagation.last_sync_s = now_s;
    std::memset(propagation.sync_request_id, 0, sizeof(propagation.sync_request_id));
}
} // namespace chat::lxmf::runtime

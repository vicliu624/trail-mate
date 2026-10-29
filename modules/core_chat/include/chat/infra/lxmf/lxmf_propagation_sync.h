#pragma once
#include "chat/infra/lxmf/lxmf_propagation_runtime.h"

namespace chat::lxmf::runtime
{
// Storage-free sync transitions. The platform owns state and serializes calls;
// remote deletion acknowledgements follow durable application acceptance.
bool propagationSyncDue(const PropagationRuntime& propagation, uint32_t now_s, uint32_t sync_interval_s);

bool startPropagationSyncIfDue(PropagationRuntime& propagation, uint32_t now_s,
                               uint32_t now_ms,
                               uint32_t sync_interval_s);

void markPropagationSyncRequestSent(PropagationRuntime& propagation,
                                    const uint8_t request_id[reticulum::kTruncatedHashSize],
                                    PropagationSyncStage next_stage);

bool propagationSyncRequestMatches(const PropagationRuntime& propagation,
                                   const LinkPendingRequest& request);

void markPropagationSyncFailed(PropagationRuntime& propagation);

void notePropagationListingResult(PropagationRuntime& propagation, const PropagationIdList& remote_ids,
                                  std::size_t max_messages);

void rememberDeliveredPropagationTransient(PropagationRuntime& propagation,
                                           const uint8_t transient_id[reticulum::kFullHashSize],
                                           uint32_t now_s,
                                           std::size_t max_transients);

void notePropagationDownloadResult(PropagationRuntime& propagation, bool registration_failed,
                                   uint32_t now_ms);

bool pollPropagationPersistence(PropagationRuntime& propagation, uint32_t now_ms, uint32_t ttl_ms);

bool notePropagationDeliveryCommit(PropagationRuntime& propagation,
                                   const uint8_t message_hash[reticulum::kFullHashSize],
                                   bool accepted,
                                   uint32_t now_s,
                                   std::size_t max_transients);

void markPropagationAcknowledged(PropagationRuntime& propagation);

void finishPropagationSyncComplete(PropagationRuntime& propagation, uint32_t now_s);

void finishPropagationSyncFailed(PropagationRuntime& propagation, uint32_t now_s);
} // namespace chat::lxmf::runtime

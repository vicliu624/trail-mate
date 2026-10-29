/**
 * @file lxmf_propagation_runtime.h
 * @brief Propagation state helpers shared by Reticulum/LXMF platform adapters
 */

#pragma once

#include "chat/infra/lxmf/lxmf_runtime_state.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace chat::lxmf::runtime
{

struct PropagationRuntimeLimits
{
    std::size_t max_entries = 0;
    std::size_t max_transients = 0;
    std::size_t max_peers = 0;
    uint32_t entry_ttl_s = 0;
    uint32_t transient_ttl_s = 0;
    uint32_t peer_ttl_s = 0;
};

struct PropagationMessageSelection
{
    RuntimeByteSpanList messages;
    uint32_t served_count = 0;
};

PropagationEntry* findPropagationEntry(
    PropagationRuntime& propagation,
    const uint8_t transient_id[reticulum::kFullHashSize]);
const PropagationEntry* findPropagationEntry(
    const PropagationRuntime& propagation,
    const uint8_t transient_id[reticulum::kFullHashSize]);

PropagationPeerState* findPropagationPeer(
    PropagationRuntime& propagation,
    const uint8_t propagation_hash[reticulum::kTruncatedHashSize]);
const PropagationPeerState* findPropagationPeer(
    const PropagationRuntime& propagation,
    const uint8_t propagation_hash[reticulum::kTruncatedHashSize]);

PropagationPeerState& upsertPropagationPeer(
    PropagationRuntime& propagation,
    const uint8_t propagation_hash[reticulum::kTruncatedHashSize],
    const uint8_t delivery_hash[reticulum::kTruncatedHashSize],
    const uint8_t identity_hash[reticulum::kTruncatedHashSize],
    std::size_t max_peers);

void markPropagationPeerSeen(PropagationPeerState& peer, uint32_t now_s);
const PropagationPeerState* selectPropagationPeer(
    const PropagationRuntime& propagation,
    bool automatic,
    const uint8_t configured_hash[reticulum::kTruncatedHashSize],
    uint32_t now_s,
    uint32_t peer_ttl_s);
void notePropagationPeerIncomingMessage(PropagationPeerState& peer);
void notePropagationPeerServedMessages(PropagationPeerState& peer,
                                       uint32_t served_count);

bool hasSeenPropagationTransient(
    const PropagationRuntime& propagation,
    const uint8_t transient_id[reticulum::kFullHashSize],
    bool* out_delivered = nullptr);
void rememberPropagationTransient(
    PropagationRuntime& propagation,
    const uint8_t transient_id[reticulum::kFullHashSize],
    bool delivered,
    uint32_t now_s,
    std::size_t max_transients);

bool awaitPropagationDeliveryCommit(
    PropagationRuntime& propagation,
    const uint8_t transient_id[reticulum::kFullHashSize],
    const uint8_t message_hash[reticulum::kFullHashSize],
    std::size_t max_pending);
bool commitPropagationDelivery(
    PropagationRuntime& propagation,
    const uint8_t message_hash[reticulum::kFullHashSize],
    bool accepted,
    uint32_t now_s,
    std::size_t max_transients);
bool propagationDeliveryCommitsResolved(const PropagationRuntime& propagation);
bool propagationDeliveryCommitRejected(const PropagationRuntime& propagation);
void clearPropagationDeliveryCommits(PropagationRuntime& propagation);

bool rememberPropagationEntry(
    PropagationRuntime& propagation,
    const uint8_t transient_id[reticulum::kFullHashSize],
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    const uint8_t* lxmf_data,
    std::size_t lxmf_len,
    uint32_t now_s,
    std::size_t max_entries);
std::size_t removePropagationEntriesForDestination(
    PropagationRuntime& propagation,
    const uint8_t transient_id[reticulum::kFullHashSize],
    const uint8_t destination_hash[reticulum::kTruncatedHashSize]);

PropagationIdList collectMissingPropagationTransientIds(
    const PropagationRuntime& propagation,
    const PropagationIdList& transient_ids);
PropagationIdList collectPropagationEntryIdsForDestination(
    const PropagationRuntime& propagation,
    const uint8_t destination_hash[reticulum::kTruncatedHashSize]);
PropagationMessageSelection collectPropagationMessagesForWants(
    PropagationRuntime& propagation,
    const PropagationIdList& transient_ids,
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    std::size_t transfer_limit_bytes,
    std::size_t base_response_size,
    std::size_t per_message_overhead);

void cullPropagationRuntime(PropagationRuntime& propagation,
                            uint32_t now_s,
                            const PropagationRuntimeLimits& limits);

} // namespace chat::lxmf::runtime

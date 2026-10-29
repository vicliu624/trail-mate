/**
 * @file lxmf_propagation_runtime.cpp
 * @brief Propagation state helpers shared by Reticulum/LXMF platform adapters
 */

#include "chat/infra/lxmf/lxmf_propagation_runtime.h"

#include <algorithm>
#include <cstring>

namespace chat::lxmf::runtime
{
namespace
{

bool hashesEqual(const uint8_t* a, const uint8_t* b, std::size_t len)
{
    if ((!a || !b) && len != 0)
    {
        return false;
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }
    return true;
}

void copyHash(uint8_t* out, const uint8_t* in, std::size_t len)
{
    if (!out || !in || len == 0)
    {
        return;
    }
    std::memcpy(out, in, len);
}

template <typename ByteBuffer>
bool validTransientId(const ByteBuffer& transient_id)
{
    return transient_id.size() == reticulum::kFullHashSize;
}

bool entryExpired(const PropagationEntry& entry,
                  uint32_t now_s,
                  const PropagationRuntimeLimits& limits)
{
    return entry.created_s == 0 || now_s < entry.created_s ||
           (now_s - entry.created_s) > limits.entry_ttl_s;
}

bool transientExpired(const PropagationTransientEntry& entry,
                      uint32_t now_s,
                      const PropagationRuntimeLimits& limits)
{
    return entry.seen_s == 0 || now_s < entry.seen_s ||
           (now_s - entry.seen_s) > limits.transient_ttl_s;
}

bool peerExpired(const PropagationPeerState& entry,
                 uint32_t now_s,
                 const PropagationRuntimeLimits& limits)
{
    return entry.last_seen_s != 0 && now_s >= entry.last_seen_s &&
           (now_s - entry.last_seen_s) > limits.peer_ttl_s;
}

} // namespace

PropagationEntry* findPropagationEntry(
    PropagationRuntime& propagation,
    const uint8_t transient_id[reticulum::kFullHashSize])
{
    if (!transient_id)
    {
        return nullptr;
    }

    for (auto& entry : propagation.entries)
    {
        if (hashesEqual(entry.transient_id, transient_id, reticulum::kFullHashSize))
        {
            return &entry;
        }
    }
    return nullptr;
}

const PropagationEntry* findPropagationEntry(
    const PropagationRuntime& propagation,
    const uint8_t transient_id[reticulum::kFullHashSize])
{
    if (!transient_id)
    {
        return nullptr;
    }

    for (const auto& entry : propagation.entries)
    {
        if (hashesEqual(entry.transient_id, transient_id, reticulum::kFullHashSize))
        {
            return &entry;
        }
    }
    return nullptr;
}

PropagationPeerState* findPropagationPeer(
    PropagationRuntime& propagation,
    const uint8_t propagation_hash[reticulum::kTruncatedHashSize])
{
    if (!propagation_hash)
    {
        return nullptr;
    }

    for (auto& peer : propagation.peers)
    {
        if (hashesEqual(peer.propagation_hash,
                        propagation_hash,
                        reticulum::kTruncatedHashSize))
        {
            return &peer;
        }
    }
    return nullptr;
}

const PropagationPeerState* findPropagationPeer(
    const PropagationRuntime& propagation,
    const uint8_t propagation_hash[reticulum::kTruncatedHashSize])
{
    if (!propagation_hash)
    {
        return nullptr;
    }

    for (const auto& peer : propagation.peers)
    {
        if (hashesEqual(peer.propagation_hash,
                        propagation_hash,
                        reticulum::kTruncatedHashSize))
        {
            return &peer;
        }
    }
    return nullptr;
}

PropagationPeerState& upsertPropagationPeer(
    PropagationRuntime& propagation,
    const uint8_t propagation_hash[reticulum::kTruncatedHashSize],
    const uint8_t delivery_hash[reticulum::kTruncatedHashSize],
    const uint8_t identity_hash[reticulum::kTruncatedHashSize],
    std::size_t max_peers)
{
    if (PropagationPeerState* peer = findPropagationPeer(propagation, propagation_hash))
    {
        if (delivery_hash)
        {
            copyHash(peer->delivery_hash, delivery_hash, sizeof(peer->delivery_hash));
        }
        if (identity_hash)
        {
            copyHash(peer->identity_hash, identity_hash, sizeof(peer->identity_hash));
        }
        return *peer;
    }

    if (max_peers != 0 && propagation.peers.size() >= max_peers)
    {
        propagation.peers.erase(propagation.peers.begin());
    }

    propagation.peers.push_back(PropagationPeerState{});
    PropagationPeerState& peer = propagation.peers.back();
    if (propagation_hash)
    {
        copyHash(peer.propagation_hash, propagation_hash, sizeof(peer.propagation_hash));
    }
    if (delivery_hash)
    {
        copyHash(peer.delivery_hash, delivery_hash, sizeof(peer.delivery_hash));
    }
    if (identity_hash)
    {
        copyHash(peer.identity_hash, identity_hash, sizeof(peer.identity_hash));
    }
    return peer;
}

void markPropagationPeerSeen(PropagationPeerState& peer, uint32_t now_s)
{
    peer.last_seen_s = now_s;
}

const PropagationPeerState* selectPropagationPeer(
    const PropagationRuntime& propagation,
    bool automatic,
    const uint8_t configured_hash[reticulum::kTruncatedHashSize],
    uint32_t now_s,
    uint32_t peer_ttl_s)
{
    const PropagationPeerState* selected = nullptr;
    for (const auto& peer : propagation.peers)
    {
        const bool expired =
            peer.last_seen_s == 0 || now_s < peer.last_seen_s ||
            (peer_ttl_s != 0 && (now_s - peer.last_seen_s) > peer_ttl_s);
        if (expired || !peer.node_active)
        {
            continue;
        }
        if (!automatic)
        {
            if (configured_hash &&
                hashesEqual(peer.propagation_hash,
                            configured_hash,
                            reticulum::kTruncatedHashSize))
            {
                return &peer;
            }
            continue;
        }
        if (!selected || peer.hops < selected->hops ||
            (peer.hops == selected->hops &&
             peer.last_seen_s > selected->last_seen_s))
        {
            selected = &peer;
        }
    }
    return selected;
}

void notePropagationPeerIncomingMessage(PropagationPeerState& peer)
{
    peer.incoming_messages += 1;
}

void notePropagationPeerServedMessages(PropagationPeerState& peer,
                                       uint32_t served_count)
{
    peer.served_messages += served_count;
}

bool hasSeenPropagationTransient(
    const PropagationRuntime& propagation,
    const uint8_t transient_id[reticulum::kFullHashSize],
    bool* out_delivered)
{
    if (out_delivered)
    {
        *out_delivered = false;
    }

    if (!transient_id)
    {
        return false;
    }

    for (const auto& entry : propagation.transients)
    {
        if (hashesEqual(entry.transient_id, transient_id, reticulum::kFullHashSize))
        {
            if (out_delivered)
            {
                *out_delivered = entry.delivered;
            }
            return true;
        }
    }
    return false;
}

void rememberPropagationTransient(
    PropagationRuntime& propagation,
    const uint8_t transient_id[reticulum::kFullHashSize],
    bool delivered,
    uint32_t now_s,
    std::size_t max_transients)
{
    if (!transient_id)
    {
        return;
    }

    for (auto& entry : propagation.transients)
    {
        if (hashesEqual(entry.transient_id, transient_id, reticulum::kFullHashSize))
        {
            entry.seen_s = now_s;
            entry.delivered = entry.delivered || delivered;
            return;
        }
    }

    if (max_transients != 0 && propagation.transients.size() >= max_transients)
    {
        propagation.transients.erase(propagation.transients.begin());
    }

    PropagationTransientEntry entry{};
    copyHash(entry.transient_id, transient_id, sizeof(entry.transient_id));
    entry.seen_s = now_s;
    entry.delivered = delivered;
    propagation.transients.push_back(entry);
}

bool awaitPropagationDeliveryCommit(
    PropagationRuntime& propagation,
    const uint8_t transient_id[reticulum::kFullHashSize],
    const uint8_t message_hash[reticulum::kFullHashSize],
    std::size_t max_pending)
{
    if (!transient_id || !message_hash || max_pending == 0)
    {
        return false;
    }

    for (auto& pending : propagation.pending_deliveries)
    {
        if (hashesEqual(pending.transient_id,
                        transient_id,
                        reticulum::kFullHashSize))
        {
            std::memcpy(pending.message_hash,
                        message_hash,
                        sizeof(pending.message_hash));
            pending.state = PropagationDeliveryCommitState::AwaitingPersistence;
            return true;
        }
    }

    if (propagation.pending_deliveries.size() >= max_pending)
    {
        return false;
    }

    PendingPropagationDelivery pending{};
    std::memcpy(pending.transient_id,
                transient_id,
                sizeof(pending.transient_id));
    std::memcpy(pending.message_hash,
                message_hash,
                sizeof(pending.message_hash));
    propagation.pending_deliveries.push_back(pending);
    return true;
}

bool commitPropagationDelivery(
    PropagationRuntime& propagation,
    const uint8_t message_hash[reticulum::kFullHashSize],
    bool accepted,
    uint32_t now_s,
    std::size_t max_transients)
{
    if (!message_hash)
    {
        return false;
    }

    bool matched = false;
    for (auto& pending : propagation.pending_deliveries)
    {
        if (!hashesEqual(pending.message_hash,
                         message_hash,
                         reticulum::kFullHashSize))
        {
            continue;
        }

        pending.state = accepted ? PropagationDeliveryCommitState::Accepted
                                 : PropagationDeliveryCommitState::Rejected;
        matched = true;
        if (accepted)
        {
            rememberPropagationTransient(propagation,
                                         pending.transient_id,
                                         true,
                                         now_s,
                                         max_transients);
            const bool already_have =
                std::any_of(propagation.sync_haves.begin(),
                            propagation.sync_haves.end(),
                            [&pending](const auto& transient_id)
                            {
                                return transient_id.size() ==
                                           reticulum::kFullHashSize &&
                                       hashesEqual(transient_id.data(),
                                                   pending.transient_id,
                                                   reticulum::kFullHashSize);
                            });
            if (!already_have)
            {
                propagation.sync_haves.emplace_back(
                    pending.transient_id,
                    pending.transient_id + reticulum::kFullHashSize);
            }
        }
    }
    return matched;
}

bool propagationDeliveryCommitsResolved(const PropagationRuntime& propagation)
{
    return !propagation.pending_deliveries.empty() &&
           std::none_of(propagation.pending_deliveries.begin(),
                        propagation.pending_deliveries.end(),
                        [](const PendingPropagationDelivery& pending)
                        {
                            return pending.state ==
                                   PropagationDeliveryCommitState::AwaitingPersistence;
                        });
}

bool propagationDeliveryCommitRejected(const PropagationRuntime& propagation)
{
    return std::any_of(propagation.pending_deliveries.begin(),
                       propagation.pending_deliveries.end(),
                       [](const PendingPropagationDelivery& pending)
                       {
                           return pending.state ==
                                  PropagationDeliveryCommitState::Rejected;
                       });
}

void clearPropagationDeliveryCommits(PropagationRuntime& propagation)
{
    propagation.pending_deliveries.clear();
}

bool rememberPropagationEntry(
    PropagationRuntime& propagation,
    const uint8_t transient_id[reticulum::kFullHashSize],
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    const uint8_t* lxmf_data,
    std::size_t lxmf_len,
    uint32_t now_s,
    std::size_t max_entries)
{
    if (!transient_id || !destination_hash || !lxmf_data || lxmf_len == 0)
    {
        return false;
    }

    if (max_entries != 0 && propagation.entries.size() >= max_entries)
    {
        propagation.entries.erase(propagation.entries.begin());
    }

    PropagationEntry entry{};
    copyHash(entry.transient_id, transient_id, sizeof(entry.transient_id));
    copyHash(entry.destination_hash, destination_hash, sizeof(entry.destination_hash));
    entry.lxmf_data.assign(lxmf_data, lxmf_data + lxmf_len);
    entry.created_s = now_s;
    propagation.entries.push_back(std::move(entry));
    return true;
}

std::size_t removePropagationEntriesForDestination(
    PropagationRuntime& propagation,
    const uint8_t transient_id[reticulum::kFullHashSize],
    const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    if (!transient_id || !destination_hash)
    {
        return 0;
    }

    const std::size_t old_size = propagation.entries.size();
    propagation.entries.erase(
        std::remove_if(propagation.entries.begin(),
                       propagation.entries.end(),
                       [transient_id, destination_hash](const PropagationEntry& entry)
                       {
                           return hashesEqual(entry.transient_id,
                                              transient_id,
                                              reticulum::kFullHashSize) &&
                                  hashesEqual(entry.destination_hash,
                                              destination_hash,
                                              reticulum::kTruncatedHashSize);
                       }),
        propagation.entries.end());
    return old_size - propagation.entries.size();
}

PropagationIdList collectMissingPropagationTransientIds(
    const PropagationRuntime& propagation,
    const PropagationIdList& transient_ids)
{
    PropagationIdList wanted_ids;
    wanted_ids.reserve(transient_ids.size());

    for (const auto& transient_id : transient_ids)
    {
        if (!validTransientId(transient_id))
        {
            continue;
        }

        if (!findPropagationEntry(propagation, transient_id.data()) &&
            !hasSeenPropagationTransient(propagation, transient_id.data(), nullptr))
        {
            wanted_ids.push_back(transient_id);
        }
    }
    return wanted_ids;
}

PropagationIdList collectPropagationEntryIdsForDestination(
    const PropagationRuntime& propagation,
    const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    PropagationIdList response_items;
    if (!destination_hash)
    {
        return response_items;
    }

    response_items.reserve(propagation.entries.size());
    for (const auto& entry : propagation.entries)
    {
        if (hashesEqual(entry.destination_hash,
                        destination_hash,
                        reticulum::kTruncatedHashSize))
        {
            response_items.emplace_back(entry.transient_id,
                                        entry.transient_id + reticulum::kFullHashSize);
        }
    }
    return response_items;
}

PropagationMessageSelection collectPropagationMessagesForWants(
    PropagationRuntime& propagation,
    const PropagationIdList& transient_ids,
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    std::size_t transfer_limit_bytes,
    std::size_t base_response_size,
    std::size_t per_message_overhead)
{
    PropagationMessageSelection selection{};
    if (!destination_hash)
    {
        return selection;
    }

    std::size_t cumulative_size = base_response_size;
    for (const auto& transient_id : transient_ids)
    {
        if (!validTransientId(transient_id))
        {
            continue;
        }

        PropagationEntry* entry = findPropagationEntry(propagation, transient_id.data());
        if (!entry ||
            !hashesEqual(entry->destination_hash,
                         destination_hash,
                         reticulum::kTruncatedHashSize))
        {
            continue;
        }

        const std::size_t next_size =
            cumulative_size + entry->lxmf_data.size() + per_message_overhead;
        if (transfer_limit_bytes != 0U && next_size > transfer_limit_bytes)
        {
            break;
        }

        selection.messages.push_back(ByteSpan{entry->lxmf_data.data(),
                                              entry->lxmf_data.size()});
        cumulative_size = next_size;
        entry->served_count += 1;
        selection.served_count += 1;
    }
    return selection;
}

void cullPropagationRuntime(PropagationRuntime& propagation,
                            uint32_t now_s,
                            const PropagationRuntimeLimits& limits)
{
    propagation.entries.erase(
        std::remove_if(propagation.entries.begin(),
                       propagation.entries.end(),
                       [now_s, &limits](const PropagationEntry& entry)
                       {
                           return entryExpired(entry, now_s, limits);
                       }),
        propagation.entries.end());

    propagation.transients.erase(
        std::remove_if(propagation.transients.begin(),
                       propagation.transients.end(),
                       [now_s, &limits](const PropagationTransientEntry& entry)
                       {
                           return transientExpired(entry, now_s, limits);
                       }),
        propagation.transients.end());

    propagation.peers.erase(
        std::remove_if(propagation.peers.begin(),
                       propagation.peers.end(),
                       [now_s, &limits](const PropagationPeerState& entry)
                       {
                           return peerExpired(entry, now_s, limits);
                       }),
        propagation.peers.end());
}

} // namespace chat::lxmf::runtime

/**
 * @file lxmf_transport_runtime.cpp
 * @brief Transport runtime table helpers for the embedded Reticulum/LXMF adapter
 */

#include "chat/infra/lxmf/lxmf_transport_runtime.h"

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

uint64_t announceTimebase(
    const uint8_t random_blob[kAnnounceRandomBlobSize])
{
    uint64_t emitted = 0;
    for (std::size_t index = 5; index < kAnnounceRandomBlobSize; ++index)
    {
        emitted = (emitted << 8U) | random_blob[index];
    }
    return emitted;
}

bool hasRandomBlob(const PathEntry& path,
                   const uint8_t random_blob[kAnnounceRandomBlobSize])
{
    for (std::size_t index = 0; index < path.announce_random_blob_count; ++index)
    {
        if (hashesEqual(path.announce_random_blobs[index],
                        random_blob,
                        kAnnounceRandomBlobSize))
        {
            return true;
        }
    }
    return false;
}

void appendRandomBlob(PathEntry& path,
                      const uint8_t random_blob[kAnnounceRandomBlobSize])
{
    if (hasRandomBlob(path, random_blob))
    {
        return;
    }

    if (path.announce_random_blob_count < kPathRandomBlobHistory)
    {
        copyHash(path.announce_random_blobs[path.announce_random_blob_count],
                 random_blob,
                 kAnnounceRandomBlobSize);
        ++path.announce_random_blob_count;
        return;
    }

    std::memmove(path.announce_random_blobs[0],
                 path.announce_random_blobs[1],
                 (kPathRandomBlobHistory - 1U) * kAnnounceRandomBlobSize);
    copyHash(path.announce_random_blobs[kPathRandomBlobHistory - 1U],
             random_blob,
             kAnnounceRandomBlobSize);
}

} // namespace

bool pathAnnounceAccepted(PathAnnounceDecision decision)
{
    return decision == PathAnnounceDecision::AcceptNew ||
           decision == PathAnnounceDecision::AcceptNewer ||
           decision == PathAnnounceDecision::AcceptExpired;
}

bool pathExpired(const PathEntry& path, uint32_t now_ms, uint32_t path_ttl_ms)
{
    if (path.updated_ms == 0)
    {
        return true;
    }
    return path_ttl_ms != 0 && (now_ms - path.updated_ms) > path_ttl_ms;
}

PathAnnounceDecision evaluatePathAnnounce(
    const PathEntry* existing,
    uint8_t hops,
    const uint8_t random_blob[kAnnounceRandomBlobSize],
    uint32_t now_ms,
    uint32_t path_ttl_ms)
{
    if (!existing)
    {
        return PathAnnounceDecision::AcceptNew;
    }
    if (!random_blob || hasRandomBlob(*existing, random_blob))
    {
        return PathAnnounceDecision::RejectReplay;
    }

    const uint64_t emitted = announceTimebase(random_blob);
    if (hops <= existing->hops)
    {
        return emitted > existing->announce_timebase
                   ? PathAnnounceDecision::AcceptNewer
                   : PathAnnounceDecision::RejectStale;
    }
    if (pathExpired(*existing, now_ms, path_ttl_ms))
    {
        return PathAnnounceDecision::AcceptExpired;
    }
    return emitted > existing->announce_timebase
               ? PathAnnounceDecision::AcceptNewer
               : PathAnnounceDecision::RejectStale;
}

void applyPathAnnounce(PathEntry& path,
                       uint8_t hops,
                       const uint8_t random_blob[kAnnounceRandomBlobSize],
                       uint32_t now_ms,
                       uint32_t last_seen_s)
{
    path.hops = hops;
    path.last_seen_s = last_seen_s;
    path.updated_ms = now_ms == 0 ? 1U : now_ms;
    if (random_blob)
    {
        path.announce_timebase =
            std::max(path.announce_timebase, announceTimebase(random_blob));
        appendRandomBlob(path, random_blob);
    }
}

bool isDuplicatePacket(const TransportRuntime& transport,
                       const uint8_t packet_hash[reticulum::kFullHashSize])
{
    if (!packet_hash)
    {
        return false;
    }
    for (const auto& entry : transport.packet_filter)
    {
        if (entry.seen_ms != 0 &&
            hashesEqual(entry.packet_hash, packet_hash, reticulum::kFullHashSize))
        {
            return true;
        }
    }
    return false;
}

void rememberPacket(TransportRuntime& transport,
                    const uint8_t packet_hash[reticulum::kFullHashSize],
                    uint32_t now_ms,
                    std::size_t max_packet_filter)
{
    if (!packet_hash)
    {
        return;
    }

    if (max_packet_filter != 0 && transport.packet_filter.size() >= max_packet_filter)
    {
        transport.packet_filter.erase(transport.packet_filter.begin());
    }

    PacketFilterEntry entry{};
    copyHash(entry.packet_hash, packet_hash, sizeof(entry.packet_hash));
    entry.seen_ms = now_ms;
    transport.packet_filter.push_back(entry);
}

void forgetPacket(TransportRuntime& transport,
                  const uint8_t packet_hash[reticulum::kFullHashSize])
{
    if (!packet_hash)
    {
        return;
    }
    transport.packet_filter.erase(
        std::remove_if(transport.packet_filter.begin(),
                       transport.packet_filter.end(),
                       [packet_hash](const PacketFilterEntry& entry)
                       {
                           return hashesEqual(entry.packet_hash,
                                              packet_hash,
                                              sizeof(entry.packet_hash));
                       }),
        transport.packet_filter.end());
}

void rememberReversePath(TransportRuntime& transport,
                         const uint8_t proof_hash[reticulum::kTruncatedHashSize],
                         uint8_t interface_id,
                         uint8_t expected_hops,
                         uint32_t now_ms,
                         std::size_t max_reverse_entries)
{
    if (!proof_hash)
    {
        return;
    }

    for (auto& entry : transport.reverse_table)
    {
        if (hashesEqual(entry.proof_hash, proof_hash, sizeof(entry.proof_hash)))
        {
            entry.interface_id = interface_id;
            entry.expected_hops = expected_hops;
            entry.created_ms = now_ms;
            return;
        }
    }

    if (max_reverse_entries != 0 && transport.reverse_table.size() >= max_reverse_entries)
    {
        transport.reverse_table.erase(transport.reverse_table.begin());
    }

    ReverseEntry entry{};
    copyHash(entry.proof_hash, proof_hash, sizeof(entry.proof_hash));
    entry.interface_id = interface_id;
    entry.expected_hops = expected_hops;
    entry.created_ms = now_ms;
    transport.reverse_table.push_back(entry);
}

ReverseEntry* findReversePath(TransportRuntime& transport,
                              const uint8_t proof_hash[reticulum::kTruncatedHashSize])
{
    if (!proof_hash)
    {
        return nullptr;
    }
    for (auto& entry : transport.reverse_table)
    {
        if (entry.created_ms != 0 &&
            hashesEqual(entry.proof_hash, proof_hash, sizeof(entry.proof_hash)))
        {
            return &entry;
        }
    }
    return nullptr;
}

PendingPathRequest* findPendingPathRequest(
    TransportRuntime& transport,
    const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    if (!destination_hash)
    {
        return nullptr;
    }

    for (auto& pending : transport.pending_path_requests)
    {
        if (hashesEqual(pending.destination_hash, destination_hash, sizeof(pending.destination_hash)))
        {
            return &pending;
        }
    }
    return nullptr;
}

const PendingPathRequest* findPendingPathRequest(
    const TransportRuntime& transport,
    const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    if (!destination_hash)
    {
        return nullptr;
    }

    for (const auto& pending : transport.pending_path_requests)
    {
        if (hashesEqual(pending.destination_hash, destination_hash, sizeof(pending.destination_hash)))
        {
            return &pending;
        }
    }
    return nullptr;
}

void notePendingPathRequest(TransportRuntime& transport,
                            const uint8_t destination_hash[reticulum::kTruncatedHashSize],
                            uint32_t now_ms,
                            std::size_t max_pending_path_requests)
{
    if (!destination_hash)
    {
        return;
    }

    PendingPathRequest* pending = findPendingPathRequest(transport, destination_hash);
    if (!pending)
    {
        if (max_pending_path_requests != 0 &&
            transport.pending_path_requests.size() >= max_pending_path_requests)
        {
            transport.pending_path_requests.erase(transport.pending_path_requests.begin());
        }

        transport.pending_path_requests.push_back(PendingPathRequest{});
        pending = &transport.pending_path_requests.back();
        copyHash(pending->destination_hash, destination_hash, sizeof(pending->destination_hash));
        pending->created_ms = now_ms;
    }

    pending->last_attempt_ms = now_ms;
    pending->resolved = false;
    if (pending->attempts < 0xFFU)
    {
        pending->attempts += 1;
    }
}

void resolvePendingPathRequest(TransportRuntime& transport,
                               const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    if (!destination_hash)
    {
        return;
    }

    transport.pending_path_requests.erase(
        std::remove_if(transport.pending_path_requests.begin(),
                       transport.pending_path_requests.end(),
                       [destination_hash](const PendingPathRequest& pending)
                       {
                           return hashesEqual(pending.destination_hash,
                                              destination_hash,
                                              sizeof(pending.destination_hash));
                       }),
        transport.pending_path_requests.end());
}

void notePendingPingReceipt(
    TransportRuntime& transport,
    const uint8_t packet_hash[reticulum::kFullHashSize],
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    const uint8_t peer_sig_pub[reticulum::kSigningPublicKeySize],
    uint32_t now_ms,
    std::size_t max_pending_ping_receipts)
{
    if (!packet_hash || !destination_hash || !peer_sig_pub)
    {
        return;
    }

    uint8_t proof_hash[reticulum::kTruncatedHashSize] = {};
    copyHash(proof_hash, packet_hash, sizeof(proof_hash));
    removePendingPingReceipt(transport, proof_hash);
    if (max_pending_ping_receipts != 0 &&
        transport.pending_ping_receipts.size() >= max_pending_ping_receipts)
    {
        transport.pending_ping_receipts.erase(transport.pending_ping_receipts.begin());
    }

    transport.pending_ping_receipts.push_back(PendingPingReceipt{});
    PendingPingReceipt& receipt = transport.pending_ping_receipts.back();
    copyHash(receipt.packet_hash, packet_hash, sizeof(receipt.packet_hash));
    copyHash(receipt.proof_hash, proof_hash, sizeof(receipt.proof_hash));
    copyHash(receipt.destination_hash,
             destination_hash,
             sizeof(receipt.destination_hash));
    copyHash(receipt.peer_sig_pub, peer_sig_pub, sizeof(receipt.peer_sig_pub));
    receipt.created_ms = now_ms == 0 ? 1U : now_ms;
}

PendingPingReceipt* findPendingPingReceipt(
    TransportRuntime& transport,
    const uint8_t proof_hash[reticulum::kTruncatedHashSize])
{
    if (!proof_hash)
    {
        return nullptr;
    }
    for (auto& receipt : transport.pending_ping_receipts)
    {
        if (receipt.created_ms != 0 &&
            hashesEqual(receipt.proof_hash, proof_hash, sizeof(receipt.proof_hash)))
        {
            return &receipt;
        }
    }
    return nullptr;
}

void removePendingPingReceipt(
    TransportRuntime& transport,
    const uint8_t proof_hash[reticulum::kTruncatedHashSize])
{
    if (!proof_hash)
    {
        return;
    }
    transport.pending_ping_receipts.erase(
        std::remove_if(transport.pending_ping_receipts.begin(),
                       transport.pending_ping_receipts.end(),
                       [proof_hash](const PendingPingReceipt& receipt)
                       {
                           return hashesEqual(receipt.proof_hash,
                                              proof_hash,
                                              sizeof(receipt.proof_hash));
                       }),
        transport.pending_ping_receipts.end());
}

PathEntry& upsertPath(TransportRuntime& transport,
                      const uint8_t destination_hash[reticulum::kTruncatedHashSize],
                      std::size_t max_paths)
{
    for (auto& path : transport.paths)
    {
        if (hashesEqual(path.destination_hash, destination_hash, reticulum::kTruncatedHashSize))
        {
            return path;
        }
    }

    if (max_paths != 0 && transport.paths.size() >= max_paths)
    {
        const auto oldest = std::min_element(
            transport.paths.begin(),
            transport.paths.end(),
            [](const PathEntry& lhs, const PathEntry& rhs)
            {
                return lhs.updated_ms < rhs.updated_ms;
            });
        transport.paths.erase(oldest);
    }

    transport.paths.push_back(PathEntry{});
    PathEntry& path = transport.paths.back();
    copyHash(path.destination_hash, destination_hash, sizeof(path.destination_hash));
    return path;
}

const PathEntry* findPath(const TransportRuntime& transport,
                          const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    if (!destination_hash)
    {
        return nullptr;
    }
    for (const auto& path : transport.paths)
    {
        if (hashesEqual(path.destination_hash, destination_hash, sizeof(path.destination_hash)))
        {
            return &path;
        }
    }
    return nullptr;
}

LinkRelayEntry& upsertLinkRelay(TransportRuntime& transport,
                                const uint8_t link_id[reticulum::kTruncatedHashSize],
                                std::size_t max_link_relays)
{
    for (auto& relay : transport.link_relays)
    {
        if (hashesEqual(relay.link_id, link_id, sizeof(relay.link_id)))
        {
            return relay;
        }
    }

    if (max_link_relays != 0 && transport.link_relays.size() >= max_link_relays)
    {
        transport.link_relays.erase(transport.link_relays.begin());
    }

    transport.link_relays.push_back(LinkRelayEntry{});
    LinkRelayEntry& relay = transport.link_relays.back();
    copyHash(relay.link_id, link_id, sizeof(relay.link_id));
    return relay;
}

LinkRelayEntry* findLinkRelay(TransportRuntime& transport,
                              const uint8_t link_id[reticulum::kTruncatedHashSize])
{
    if (!link_id)
    {
        return nullptr;
    }
    for (auto& relay : transport.link_relays)
    {
        if (hashesEqual(relay.link_id, link_id, sizeof(relay.link_id)))
        {
            return &relay;
        }
    }
    return nullptr;
}

void cullTransportRuntime(TransportRuntime& transport,
                          uint32_t now_ms,
                          const TransportRuntimeLimits& limits)
{
    if (limits.path_ttl_ms != 0)
    {
        transport.paths.erase(
            std::remove_if(transport.paths.begin(),
                           transport.paths.end(),
                           [now_ms, &limits](const PathEntry& path)
                           {
                               return pathExpired(path, now_ms, limits.path_ttl_ms);
                           }),
            transport.paths.end());
    }

    transport.packet_filter.erase(
        std::remove_if(transport.packet_filter.begin(), transport.packet_filter.end(),
                       [now_ms, &limits](const PacketFilterEntry& entry)
                       {
                           return entry.seen_ms == 0 ||
                                  (now_ms - entry.seen_ms) > limits.packet_filter_ttl_ms;
                       }),
        transport.packet_filter.end());

    transport.reverse_table.erase(
        std::remove_if(transport.reverse_table.begin(), transport.reverse_table.end(),
                       [now_ms, &limits](const ReverseEntry& entry)
                       {
                           return entry.created_ms == 0 ||
                                  (now_ms - entry.created_ms) > limits.reverse_entry_ttl_ms;
                       }),
        transport.reverse_table.end());

    transport.link_relays.erase(
        std::remove_if(transport.link_relays.begin(), transport.link_relays.end(),
                       [now_ms, &limits](const LinkRelayEntry& entry)
                       {
                           return entry.last_seen_ms == 0 ||
                                  (now_ms - entry.last_seen_ms) > limits.link_relay_ttl_ms;
                       }),
        transport.link_relays.end());

    transport.pending_path_requests.erase(
        std::remove_if(transport.pending_path_requests.begin(),
                       transport.pending_path_requests.end(),
                       [now_ms, &limits](const PendingPathRequest& pending)
                       {
                           return pending.created_ms == 0 ||
                                  (now_ms - pending.created_ms) >
                                      limits.pending_path_request_ttl_ms;
                       }),
        transport.pending_path_requests.end());

    if (limits.pending_ping_receipt_ttl_ms != 0)
    {
        transport.pending_ping_receipts.erase(
            std::remove_if(transport.pending_ping_receipts.begin(),
                           transport.pending_ping_receipts.end(),
                           [now_ms, &limits](const PendingPingReceipt& receipt)
                           {
                               return receipt.created_ms == 0 ||
                                      (now_ms - receipt.created_ms) >
                                          limits.pending_ping_receipt_ttl_ms;
                           }),
            transport.pending_ping_receipts.end());
    }
    if (limits.max_pending_ping_receipts != 0 &&
        transport.pending_ping_receipts.size() > limits.max_pending_ping_receipts)
    {
        const std::size_t excess =
            transport.pending_ping_receipts.size() - limits.max_pending_ping_receipts;
        transport.pending_ping_receipts.erase(
            transport.pending_ping_receipts.begin(),
            transport.pending_ping_receipts.begin() + excess);
    }
}

} // namespace chat::lxmf::runtime

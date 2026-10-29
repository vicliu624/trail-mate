/**
 * @file lxmf_transport_runtime.h
 * @brief Transport runtime table helpers for the embedded Reticulum/LXMF adapter
 */

#pragma once

#include "chat/infra/lxmf/lxmf_runtime_state.h"

#include <cstddef>
#include <cstdint>

namespace chat::lxmf::runtime
{

struct TransportRuntimeLimits
{
    std::size_t max_paths = 0;
    std::size_t max_packet_filter = 0;
    std::size_t max_reverse_entries = 0;
    std::size_t max_link_relays = 0;
    std::size_t max_pending_path_requests = 0;
    uint32_t packet_filter_ttl_ms = 0;
    uint32_t pending_path_request_ttl_ms = 0;
    uint32_t reverse_entry_ttl_ms = 0;
    uint32_t link_relay_ttl_ms = 0;
    std::size_t max_pending_ping_receipts = 0;
    uint32_t path_ttl_ms = 0;
    uint32_t pending_ping_receipt_ttl_ms = 0;
};

enum class PathAnnounceDecision : uint8_t
{
    AcceptNew = 0,
    AcceptNewer = 1,
    AcceptExpired = 2,
    RejectReplay = 3,
    RejectStale = 4
};

bool pathAnnounceAccepted(PathAnnounceDecision decision);
bool pathExpired(const PathEntry& path, uint32_t now_ms, uint32_t path_ttl_ms);
PathAnnounceDecision evaluatePathAnnounce(
    const PathEntry* existing,
    uint8_t hops,
    const uint8_t random_blob[kAnnounceRandomBlobSize],
    uint32_t now_ms,
    uint32_t path_ttl_ms);
void applyPathAnnounce(PathEntry& path,
                       uint8_t hops,
                       const uint8_t random_blob[kAnnounceRandomBlobSize],
                       uint32_t now_ms,
                       uint32_t last_seen_s);

bool isDuplicatePacket(const TransportRuntime& transport,
                       const uint8_t packet_hash[reticulum::kFullHashSize]);
void rememberPacket(TransportRuntime& transport,
                    const uint8_t packet_hash[reticulum::kFullHashSize],
                    uint32_t now_ms,
                    std::size_t max_packet_filter);
void forgetPacket(TransportRuntime& transport,
                  const uint8_t packet_hash[reticulum::kFullHashSize]);

void rememberReversePath(TransportRuntime& transport,
                         const uint8_t proof_hash[reticulum::kTruncatedHashSize],
                         uint8_t interface_id,
                         uint8_t expected_hops,
                         uint32_t now_ms,
                         std::size_t max_reverse_entries);
ReverseEntry* findReversePath(TransportRuntime& transport,
                              const uint8_t proof_hash[reticulum::kTruncatedHashSize]);

PendingPathRequest* findPendingPathRequest(
    TransportRuntime& transport,
    const uint8_t destination_hash[reticulum::kTruncatedHashSize]);
const PendingPathRequest* findPendingPathRequest(
    const TransportRuntime& transport,
    const uint8_t destination_hash[reticulum::kTruncatedHashSize]);
void notePendingPathRequest(TransportRuntime& transport,
                            const uint8_t destination_hash[reticulum::kTruncatedHashSize],
                            uint32_t now_ms,
                            std::size_t max_pending_path_requests);
void resolvePendingPathRequest(TransportRuntime& transport,
                               const uint8_t destination_hash[reticulum::kTruncatedHashSize]);

void notePendingPingReceipt(
    TransportRuntime& transport,
    const uint8_t packet_hash[reticulum::kFullHashSize],
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    const uint8_t peer_sig_pub[reticulum::kSigningPublicKeySize],
    uint32_t now_ms,
    std::size_t max_pending_ping_receipts);
PendingPingReceipt* findPendingPingReceipt(
    TransportRuntime& transport,
    const uint8_t proof_hash[reticulum::kTruncatedHashSize]);
void removePendingPingReceipt(
    TransportRuntime& transport,
    const uint8_t proof_hash[reticulum::kTruncatedHashSize]);

PathEntry& upsertPath(TransportRuntime& transport,
                      const uint8_t destination_hash[reticulum::kTruncatedHashSize],
                      std::size_t max_paths);
const PathEntry* findPath(const TransportRuntime& transport,
                          const uint8_t destination_hash[reticulum::kTruncatedHashSize]);

LinkRelayEntry& upsertLinkRelay(TransportRuntime& transport,
                                const uint8_t link_id[reticulum::kTruncatedHashSize],
                                std::size_t max_link_relays);
LinkRelayEntry* findLinkRelay(TransportRuntime& transport,
                              const uint8_t link_id[reticulum::kTruncatedHashSize]);

void cullTransportRuntime(TransportRuntime& transport,
                          uint32_t now_ms,
                          const TransportRuntimeLimits& limits);

} // namespace chat::lxmf::runtime

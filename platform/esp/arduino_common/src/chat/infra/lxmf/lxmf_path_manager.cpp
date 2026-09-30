/**
 * @file lxmf_path_manager.cpp
 * @brief Path, packet-filter, proof-route, receipt, and link-relay owner.
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_path_manager.h"

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

} // namespace

bool PathManager::isDuplicatePacket(
    const uint8_t packet_hash[reticulum::kFullHashSize]) const
{
    return runtime::isDuplicatePacket(transport_, packet_hash);
}

void PathManager::rememberPacket(
    const uint8_t packet_hash[reticulum::kFullHashSize],
    uint32_t now_ms,
    std::size_t max_packet_filter)
{
    runtime::rememberPacket(transport_, packet_hash, now_ms, max_packet_filter);
}

void PathManager::forgetPacket(
    const uint8_t packet_hash[reticulum::kFullHashSize])
{
    runtime::forgetPacket(transport_, packet_hash);
}

void PathManager::rememberReversePath(
    const uint8_t proof_hash[reticulum::kTruncatedHashSize],
    uint8_t interface_id,
    uint8_t expected_hops,
    uint32_t now_ms,
    std::size_t max_reverse_entries)
{
    runtime::rememberReversePath(transport_,
                                 proof_hash,
                                 interface_id,
                                 expected_hops,
                                 now_ms,
                                 max_reverse_entries);
}

ReverseEntry* PathManager::findReversePath(
    const uint8_t proof_hash[reticulum::kTruncatedHashSize])
{
    return runtime::findReversePath(transport_, proof_hash);
}

PendingPathRequest* PathManager::findPendingPathRequest(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    return runtime::findPendingPathRequest(transport_, destination_hash);
}

const PendingPathRequest* PathManager::findPendingPathRequest(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize]) const
{
    return runtime::findPendingPathRequest(transport_, destination_hash);
}

void PathManager::notePendingPathRequest(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    uint32_t now_ms,
    std::size_t max_pending_path_requests)
{
    runtime::notePendingPathRequest(transport_,
                                    destination_hash,
                                    now_ms,
                                    max_pending_path_requests);
}

void PathManager::resolvePendingPathRequest(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    runtime::resolvePendingPathRequest(transport_, destination_hash);
}

bool PathManager::pendingPathRequestCoolingDown(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    uint32_t now_ms,
    uint32_t retry_interval_ms) const
{
    const PendingPathRequest* pending =
        runtime::findPendingPathRequest(transport_, destination_hash);
    return pending && !pending->resolved && pending->last_attempt_ms != 0 &&
           (now_ms - pending->last_attempt_ms) < retry_interval_ms;
}

bool PathManager::shouldRequestPeerPath(
    const PeerInfo& peer,
    uint32_t now_ms,
    uint32_t now_s,
    uint32_t pending_request_ttl_ms,
    uint32_t min_request_interval_ms,
    uint32_t path_ttl_ms,
    uint32_t refresh_age_s) const
{
    if (pendingPathRequestCoolingDown(peer.destination_hash,
                                      now_ms,
                                      min_request_interval_ms))
    {
        return false;
    }

    const PendingPathRequest* pending =
        runtime::findPendingPathRequest(transport_, peer.destination_hash);
    if (pending && !pending->resolved && pending->created_ms != 0 &&
        (now_ms - pending->created_ms) < pending_request_ttl_ms)
    {
        return false;
    }

    if (peer.last_path_request_ms != 0 &&
        (now_ms - peer.last_path_request_ms) < min_request_interval_ms)
    {
        return false;
    }

    const PathEntry* path = findPath(peer.destination_hash, now_ms, path_ttl_ms);
    if (!path || path->last_seen_s == 0)
    {
        return true;
    }

    if (now_s < path->last_seen_s)
    {
        return true;
    }

    return (now_s - path->last_seen_s) >= refresh_age_s;
}

void PathManager::notePeerPathRequest(PeerInfo& peer, uint32_t now_ms) const
{
    peer.last_path_request_ms = now_ms;
}

void PathManager::resetPeerPathRequest(PeerInfo& peer) const
{
    peer.last_path_request_ms = 0;
}

void PathManager::notePendingPingReceipt(
    const uint8_t packet_hash[reticulum::kFullHashSize],
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    const uint8_t peer_sig_pub[LxmfIdentity::kSigPubKeySize],
    uint32_t now_ms,
    std::size_t max_pending_ping_receipts)
{
    runtime::notePendingPingReceipt(transport_,
                                    packet_hash,
                                    destination_hash,
                                    peer_sig_pub,
                                    now_ms,
                                    max_pending_ping_receipts);
}

PendingPingReceipt* PathManager::findPendingPingReceipt(
    const uint8_t proof_hash[reticulum::kTruncatedHashSize])
{
    return runtime::findPendingPingReceipt(transport_, proof_hash);
}

void PathManager::removePendingPingReceipt(
    const uint8_t proof_hash[reticulum::kTruncatedHashSize])
{
    runtime::removePendingPingReceipt(transport_, proof_hash);
}

PathEntry& PathManager::upsertPath(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    std::size_t max_paths)
{
    return runtime::upsertPath(transport_, destination_hash, max_paths);
}

PathEntry* PathManager::observeAnnouncePath(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    uint8_t hops,
    const uint8_t random_hash[10],
    uint32_t now_ms,
    uint32_t now_s,
    uint8_t ingress_interface_id,
    const uint8_t* next_hop_transport,
    const uint8_t* raw_packet,
    std::size_t raw_len,
    std::size_t max_paths)
{
    if (!destination_hash || !random_hash)
    {
        return nullptr;
    }

    PathEntry& path = runtime::upsertPath(transport_, destination_hash, max_paths);
    runtime::applyPathAnnounce(path, hops, random_hash, now_ms, now_s);
    path.interface_id = ingress_interface_id;
    path.direct = next_hop_transport == nullptr;
    runtime::resolvePendingPathRequest(transport_, destination_hash);
    if (next_hop_transport)
    {
        std::memcpy(path.next_hop_transport,
                    next_hop_transport,
                    sizeof(path.next_hop_transport));
    }
    else
    {
        std::memcpy(path.next_hop_transport,
                    destination_hash,
                    sizeof(path.next_hop_transport));
    }
    if (raw_packet && raw_len <= sizeof(path.cached_announce))
    {
        std::memcpy(path.cached_announce, raw_packet, raw_len);
        path.cached_announce_len = raw_len;
        reticulum::computePacketHash(raw_packet,
                                     raw_len,
                                     path.cached_packet_hash);
    }
    return &path;
}

const PathEntry* PathManager::findPath(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    uint32_t now_ms,
    uint32_t path_ttl_ms) const
{
    const PathEntry* path = runtime::findPath(transport_, destination_hash);
    return path && !runtime::pathExpired(*path, now_ms, path_ttl_ms)
               ? path
               : nullptr;
}

const PathEntry* PathManager::findAnyPath(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize]) const
{
    return runtime::findPath(transport_, destination_hash);
}

void PathManager::expirePath(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    if (!destination_hash)
    {
        return;
    }
    transport_.paths.erase(
        std::remove_if(transport_.paths.begin(),
                       transport_.paths.end(),
                       [destination_hash](const PathEntry& path)
                       {
                           return hashesEqual(path.destination_hash,
                                              destination_hash,
                                              sizeof(path.destination_hash));
                       }),
        transport_.paths.end());
    resolvePendingPathRequest(destination_hash);
}

void PathManager::clearPaths()
{
    transport_.paths.clear();
}

void PathManager::retireInterface(uint8_t interface_id)
{
    if (!interface_id) return;
    for (const auto& path : transport_.paths)
    {
        if (path.interface_id != interface_id) continue;
        resolvePendingPathRequest(path.destination_hash);
        forgetPacket(path.cached_packet_hash);
    }
    transport_.paths.erase(std::remove_if(transport_.paths.begin(), transport_.paths.end(),
                                          [interface_id](const PathEntry& path)
                                          { return path.interface_id == interface_id; }),
                           transport_.paths.end());
    transport_.reverse_table.erase(std::remove_if(transport_.reverse_table.begin(), transport_.reverse_table.end(),
                                                  [interface_id](const ReverseEntry& entry)
                                                  { return entry.interface_id == interface_id; }),
                                   transport_.reverse_table.end());
    transport_.link_relays.erase(std::remove_if(transport_.link_relays.begin(), transport_.link_relays.end(),
                                                [interface_id](const LinkRelayEntry& entry)
                                                { return entry.initiator_interface_id == interface_id || entry.responder_interface_id == interface_id; }),
                                 transport_.link_relays.end());
}

LinkRelayEntry& PathManager::upsertLinkRelay(
    const uint8_t link_id[reticulum::kTruncatedHashSize],
    std::size_t max_link_relays)
{
    return runtime::upsertLinkRelay(transport_, link_id, max_link_relays);
}

LinkRelayEntry* PathManager::findLinkRelay(
    const uint8_t link_id[reticulum::kTruncatedHashSize])
{
    return runtime::findLinkRelay(transport_, link_id);
}

void PathManager::removeLinkRelay(
    const uint8_t link_id[reticulum::kTruncatedHashSize])
{
    if (!link_id)
    {
        return;
    }
    transport_.link_relays.erase(
        std::remove_if(transport_.link_relays.begin(),
                       transport_.link_relays.end(),
                       [link_id](const LinkRelayEntry& relay)
                       {
                           return hashesEqual(relay.link_id,
                                              link_id,
                                              sizeof(relay.link_id));
                       }),
        transport_.link_relays.end());
}

void PathManager::clearReversePathAndRelays()
{
    transport_.reverse_table.clear();
    transport_.pending_path_requests.clear();
    transport_.link_relays.clear();
}

void PathManager::cull(uint32_t now_ms, const TransportRuntimeLimits& limits)
{
    runtime::cullTransportRuntime(transport_, now_ms, limits);
}

void PathManager::clear()
{
    transport_ = TransportRuntime{};
}

} // namespace chat::lxmf::runtime

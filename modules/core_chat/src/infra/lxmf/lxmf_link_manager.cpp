/**
 * @file lxmf_link_manager.cpp
 * @brief Link session owner for the embedded LXMF runtime.
 */

#include "chat/infra/lxmf/lxmf_link_manager.h"

#include <algorithm>
#include <cstring>
#include <utility>

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

void copyBytes(uint8_t* out, const uint8_t* in, std::size_t len)
{
    if (!out || !in || len == 0)
    {
        return;
    }
    std::memcpy(out, in, len);
}

} // namespace

std::size_t LinkManager::size() const
{
    return links_.sessions.size();
}

void LinkManager::clear()
{
    links_.sessions.clear();
}

LinkSession* LinkManager::findSession(
    const uint8_t link_id[reticulum::kTruncatedHashSize])
{
    return runtime::findLinkSession(links_, link_id);
}

LinkSession* LinkManager::findOpenSessionByDestination(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    LocalDestinationKind kind)
{
    return runtime::findOpenLinkSessionByDestination(links_,
                                                     destination_hash,
                                                     kind);
}

LinkSession* LinkManager::findActiveSessionByDestination(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    LocalDestinationKind kind)
{
    return runtime::findActiveLinkSessionByDestination(links_,
                                                       destination_hash,
                                                       kind);
}

bool LinkManager::ensureCapacity(
    std::size_t max_link_sessions,
    const uint8_t preserve_link_id[reticulum::kTruncatedHashSize])
{
    if (max_link_sessions == 0 || links_.sessions.size() < max_link_sessions)
    {
        return true;
    }

    auto discard = links_.sessions.begin();
    if (preserve_link_id)
    {
        discard = std::find_if(
            links_.sessions.begin(),
            links_.sessions.end(),
            [preserve_link_id](const LinkSession& candidate)
            {
                return !hashesEqual(candidate.link_id,
                                    preserve_link_id,
                                    sizeof(candidate.link_id));
            });
        if (discard == links_.sessions.end())
        {
            return false;
        }
    }
    links_.sessions.erase(discard);
    return true;
}

LinkSession* LinkManager::appendSession(std::size_t max_link_sessions)
{
    return appendSessionPreserving(max_link_sessions, nullptr);
}

LinkSession* LinkManager::appendSessionPreserving(
    std::size_t max_link_sessions,
    const uint8_t preserve_link_id[reticulum::kTruncatedHashSize])
{
    if (!ensureCapacity(max_link_sessions, preserve_link_id))
    {
        return nullptr;
    }
    links_.sessions.push_back(LinkSession{});
    return &links_.sessions.back();
}

void LinkManager::initialiseSession(LinkSession& session,
                                    const LinkSessionSpec& spec)
{
    session = LinkSession{};
    copyBytes(session.link_id,
              spec.link_id,
              sizeof(session.link_id));
    copyBytes(session.remote_destination_hash,
              spec.remote_destination_hash,
              sizeof(session.remote_destination_hash));
    copyBytes(session.remote_identity_hash,
              spec.remote_identity_hash,
              sizeof(session.remote_identity_hash));
    copyBytes(session.local_sig_pub,
              spec.local_sig_pub,
              sizeof(session.local_sig_pub));
    copyBytes(session.peer_enc_pub,
              spec.peer_enc_pub,
              sizeof(session.peer_enc_pub));
    copyBytes(session.peer_link_sig_pub,
              spec.peer_link_sig_pub,
              sizeof(session.peer_link_sig_pub));
    copyBytes(session.peer_identity_sig_pub,
              spec.peer_identity_sig_pub,
              sizeof(session.peer_identity_sig_pub));

    session.created_ms = spec.now_ms;
    session.request_ms = spec.now_ms;
    session.last_inbound_ms = spec.now_ms;
    session.last_outbound_ms = 0;
    session.keepalive_interval_ms = spec.keepalive_interval_ms;
    session.stale_timeout_ms = spec.stale_timeout_ms;
    session.mtu = spec.mtu;
    session.mdu = spec.mdu;
    session.interface_id = spec.interface_id;
    session.expected_hops = spec.expected_hops;
    session.destination = spec.destination;
    session.state = spec.state;
    session.close_reason = LinkCloseReason::None;
    session.initiator = spec.initiator;
    session.remote_identity_known = spec.remote_identity_known;
    session.validated = spec.validated;
}

LinkSession* LinkManager::openSession(std::size_t max_link_sessions,
                                      const LinkSessionSpec& spec)
{
    return openSessionPreserving(max_link_sessions, spec, nullptr);
}

LinkSession* LinkManager::openSessionPreserving(
    std::size_t max_link_sessions,
    const LinkSessionSpec& spec,
    const uint8_t preserve_link_id[reticulum::kTruncatedHashSize])
{
    LinkSession* session =
        appendSessionPreserving(max_link_sessions, preserve_link_id);
    if (!session)
    {
        return nullptr;
    }
    initialiseSession(*session, spec);
    return session;
}

bool LinkManager::discardSession(LinkSession& session)
{
    const auto it =
        std::find_if(links_.sessions.begin(),
                     links_.sessions.end(),
                     [&session](LinkSession& candidate)
                     {
                         return &candidate == &session;
                     });
    if (it == links_.sessions.end())
    {
        return false;
    }
    links_.sessions.erase(it);
    return true;
}

bool LinkManager::closeSession(LinkSession& session,
                               LinkCloseReason reason,
                               uint32_t now_ms)
{
    return runtime::closeLinkSession(session, reason, now_ms);
}

LinkPendingRequest* LinkManager::queuePendingRequest(
    LinkSession& session,
    const uint8_t* request_id,
    std::size_t request_id_len,
    uint32_t created_ms,
    bool awaiting_resource)
{
    if (!request_id && request_id_len != 0)
    {
        return nullptr;
    }

    LinkPendingRequest request{};
    if (request_id_len != 0)
    {
        request.request_id.assign(request_id, request_id + request_id_len);
    }
    request.created_ms = created_ms;
    request.awaiting_resource = awaiting_resource;
    session.pending_requests.push_back(std::move(request));
    return &session.pending_requests.back();
}

LinkPendingRequest* LinkManager::findPendingRequest(
    LinkSession& session,
    const uint8_t* request_id,
    std::size_t request_id_len)
{
    if (!request_id && request_id_len != 0)
    {
        return nullptr;
    }
    return findPendingRequestIf(
        session,
        [request_id, request_id_len](const LinkPendingRequest& request)
        {
            return request.request_id.size() == request_id_len &&
                   (request_id_len == 0 ||
                    std::memcmp(request.request_id.data(),
                                request_id,
                                request_id_len) == 0);
        });
}

const LinkPendingRequest* LinkManager::findPendingRequest(
    const LinkSession& session,
    const uint8_t* request_id,
    std::size_t request_id_len) const
{
    if (!request_id && request_id_len != 0)
    {
        return nullptr;
    }
    return findPendingRequestIf(
        session,
        [request_id, request_id_len](const LinkPendingRequest& request)
        {
            return request.request_id.size() == request_id_len &&
                   (request_id_len == 0 ||
                    std::memcmp(request.request_id.data(),
                                request_id,
                                request_id_len) == 0);
        });
}

bool LinkManager::markPendingResponseReady(LinkSession& session,
                                           const uint8_t* request_id,
                                           std::size_t request_id_len,
                                           const uint8_t* response_data,
                                           std::size_t response_len,
                                           bool data_is_nil)
{
    LinkPendingRequest* request =
        findPendingRequest(session, request_id, request_id_len);
    if (!request)
    {
        return false;
    }

    request->response_ready = true;
    request->response.clear();
    if (!data_is_nil && response_data && response_len != 0)
    {
        request->response.assign(response_data, response_data + response_len);
    }
    return true;
}

bool LinkManager::erasePendingRequest(LinkSession& session,
                                      const LinkPendingRequest& request)
{
    auto it = std::find_if(
        session.pending_requests.begin(),
        session.pending_requests.end(),
        [&request](const LinkPendingRequest& candidate)
        {
            return &candidate == &request;
        });
    if (it == session.pending_requests.end())
    {
        return false;
    }
    session.pending_requests.erase(it);
    return true;
}

std::size_t LinkManager::pendingRequestCount(const LinkSession& session) const
{
    return session.pending_requests.size();
}

DeferredLinkPayload* LinkManager::appendDeferredPayload(
    LinkSession& session,
    DeferredLinkPayload&& payload)
{
    session.deferred_payloads.push_back(std::move(payload));
    return &session.deferred_payloads.back();
}

const DeferredLinkPayload* LinkManager::firstDeferredPayload(
    const LinkSession& session) const
{
    return session.deferred_payloads.empty() ? nullptr
                                             : &session.deferred_payloads.front();
}

bool LinkManager::popFirstDeferredPayload(LinkSession& session)
{
    if (session.deferred_payloads.empty())
    {
        return false;
    }
    session.deferred_payloads.erase(session.deferred_payloads.begin());
    return true;
}

std::size_t LinkManager::deferredPayloadCount(const LinkSession& session) const
{
    return session.deferred_payloads.size();
}

void LinkManager::touchInbound(LinkSession& session, uint32_t now_ms)
{
    session.last_inbound_ms = now_ms;
}

void LinkManager::touchOutbound(LinkSession& session, uint32_t now_ms)
{
    session.last_outbound_ms = now_ms;
}

void LinkManager::noteKeepaliveSent(LinkSession& session, uint32_t now_ms)
{
    session.last_keepalive_ms = now_ms;
}

void LinkManager::markSessionValidatedActive(LinkSession& session,
                                             float rtt_s,
                                             uint32_t keepalive_interval_ms)
{
    session.rtt_s = rtt_s;
    session.validated = true;
    session.keepalive_interval_ms = keepalive_interval_ms;
    session.stale_timeout_ms = keepalive_interval_ms * 2U;
    session.last_keepalive_ms = 0;
    session.state = LinkState::Active;
}

bool LinkManager::reactivateSessionIfStale(LinkSession& session)
{
    if (session.state != LinkState::Stale)
    {
        return false;
    }
    session.state = LinkState::Active;
    return true;
}

void LinkManager::cullSessionTables(LinkSession& session,
                                    uint32_t now_ms,
                                    const LinkRuntimeLimits& limits)
{
    runtime::cullLinkSessionTables(session, now_ms, limits);
}

void LinkManager::cullResources(LinkSession& session,
                                uint32_t now_ms,
                                const ResourceRuntimeLimits& limits)
{
    runtime::cullLinkResources(session, now_ms, limits);
}

LinkRuntimeMaintenance LinkManager::advanceSessionLifecycle(
    LinkSession& session,
    uint32_t now_ms,
    const LinkRuntimeLimits& limits)
{
    return runtime::advanceLinkSessionLifecycle(session, now_ms, limits);
}

void LinkManager::markSessionStale(LinkSession& session)
{
    runtime::markLinkSessionStale(session);
}

void LinkManager::removeExpiredSessions(uint32_t now_ms,
                                        const LinkRuntimeLimits& limits)
{
    runtime::removeExpiredLinkSessions(links_, now_ms, limits);
}

LinkResourceTransfer* LinkManager::findIncomingResource(
    LinkSession& session,
    const uint8_t resource_hash[reticulum::kFullHashSize])
{
    return runtime::findLinkResource(session.incoming_resources, resource_hash);
}

const LinkResourceTransfer* LinkManager::findIncomingResource(
    const LinkSession& session,
    const uint8_t resource_hash[reticulum::kFullHashSize]) const
{
    return runtime::findLinkResource(session.incoming_resources, resource_hash);
}

LinkResourceTransfer* LinkManager::findOutgoingResource(
    LinkSession& session,
    const uint8_t resource_hash[reticulum::kFullHashSize])
{
    return runtime::findLinkResource(session.outgoing_resources, resource_hash);
}

const LinkResourceTransfer* LinkManager::findOutgoingResource(
    const LinkSession& session,
    const uint8_t resource_hash[reticulum::kFullHashSize]) const
{
    return runtime::findLinkResource(session.outgoing_resources, resource_hash);
}

bool LinkManager::eraseIncomingResource(
    LinkSession& session,
    const uint8_t resource_hash[reticulum::kFullHashSize])
{
    return runtime::eraseLinkResourceByHash(session.incoming_resources, resource_hash);
}

bool LinkManager::eraseOutgoingResource(
    LinkSession& session,
    const uint8_t resource_hash[reticulum::kFullHashSize])
{
    return runtime::eraseLinkResourceByHash(session.outgoing_resources, resource_hash);
}

LinkResourceTransfer* LinkManager::startIncomingResource(
    LinkSession& session,
    const uint8_t resource_hash[reticulum::kFullHashSize],
    const uint8_t random_hash[kResourceMapHashLen],
    const uint8_t original_hash[reticulum::kFullHashSize],
    const uint8_t* request_id,
    std::size_t request_id_len,
    const uint8_t* hashmap,
    std::size_t hashmap_len,
    uint32_t data_size,
    uint32_t transfer_size,
    uint32_t part_count,
    uint32_t segment_index,
    uint32_t total_segments,
    uint8_t flags,
    bool encrypted,
    bool compressed,
    bool has_metadata,
    bool split,
    uint32_t now_ms,
    uint32_t window_size)
{
    LinkResourceTransfer resource{};
    if (!runtime::initialiseIncomingResourceTransfer(resource,
                                                     resource_hash,
                                                     random_hash,
                                                     original_hash,
                                                     request_id,
                                                     request_id_len,
                                                     hashmap,
                                                     hashmap_len,
                                                     data_size,
                                                     transfer_size,
                                                     part_count,
                                                     segment_index,
                                                     total_segments,
                                                     flags,
                                                     encrypted,
                                                     compressed,
                                                     has_metadata,
                                                     split,
                                                     now_ms,
                                                     window_size))
    {
        return nullptr;
    }
    session.incoming_resources.push_back(std::move(resource));
    return &session.incoming_resources.back();
}

bool LinkManager::initialiseOutgoingResource(LinkResourceTransfer& resource,
                                             const uint8_t* request_id,
                                             std::size_t request_id_len,
                                             uint32_t data_size,
                                             uint32_t transfer_size,
                                             uint32_t part_count,
                                             uint8_t flags,
                                             uint32_t now_ms,
                                             uint32_t window_size)
{
    return runtime::initialiseOutgoingResourceTransfer(resource,
                                                       request_id,
                                                       request_id_len,
                                                       data_size,
                                                       transfer_size,
                                                       part_count,
                                                       flags,
                                                       now_ms,
                                                       window_size);
}

LinkResourceTransfer* LinkManager::appendOutgoingResource(
    LinkSession& session,
    LinkResourceTransfer&& resource)
{
    session.outgoing_resources.push_back(std::move(resource));
    return &session.outgoing_resources.back();
}

LinkResourceTransfer* LinkManager::createOutgoingResource(LinkSession& session)
{
    session.outgoing_resources.emplace_back();
    return &session.outgoing_resources.back();
}
bool LinkManager::discardLastOutgoingResource(LinkSession& session)
{
    if (session.outgoing_resources.empty())
    {
        return false;
    }
    session.outgoing_resources.pop_back();
    return true;
}

ResourceWindowRequest LinkManager::buildNextResourceWindowRequest(
    const LinkResourceTransfer& resource) const
{
    return runtime::buildNextResourceWindowRequest(resource);
}

void LinkManager::noteResourceWindowRequested(LinkResourceTransfer& resource,
                                              bool waiting_for_hashmap,
                                              uint32_t now_ms)
{
    runtime::noteResourceWindowRequest(resource, waiting_for_hashmap, now_ms);
}

bool LinkManager::applyIncomingResourceHashmapUpdate(
    LinkResourceTransfer& resource,
    uint32_t segment,
    const uint8_t* hashmap,
    std::size_t hashmap_len,
    std::size_t segment_capacity,
    uint32_t now_ms)
{
    return runtime::applyResourceHashmapUpdate(resource,
                                               segment,
                                               hashmap,
                                               hashmap_len,
                                               segment_capacity,
                                               now_ms);
}

bool LinkManager::recordIncomingResourcePart(
    LinkResourceTransfer& resource,
    const uint8_t* payload,
    std::size_t payload_len,
    const uint8_t full_hash[reticulum::kFullHashSize],
    uint32_t now_ms,
    std::size_t* out_matched_index,
    bool* out_complete)
{
    return runtime::recordResourcePart(resource,
                                       payload,
                                       payload_len,
                                       full_hash,
                                       now_ms,
                                       out_matched_index,
                                       out_complete);
}

void LinkManager::markResourceComplete(LinkResourceTransfer& resource,
                                       uint32_t now_ms)
{
    runtime::markResourceComplete(resource, now_ms);
}

ResourceAssemblyResult LinkManager::appendResourceAssemblySegment(
    LinkSession& session,
    LinkResourceTransfer& resource,
    ResourcePayloadBuffer& payload_data,
    uint32_t now_ms)
{
    return runtime::appendResourceAssemblySegment(session,
                                                  resource,
                                                  payload_data,
                                                  now_ms);
}

bool LinkManager::markOutgoingResourceProofReceived(
    LinkResourceTransfer& resource,
    const uint8_t expected_proof[reticulum::kFullHashSize],
    uint32_t now_ms)
{
    return runtime::markResourceProofReceived(resource, expected_proof, now_ms);
}

void LinkManager::touchResource(LinkResourceTransfer& resource, uint32_t now_ms)
{
    resource.last_activity_ms = now_ms;
}

} // namespace chat::lxmf::runtime

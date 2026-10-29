/**
 * @file lxmf_link_runtime.cpp
 * @brief Link runtime lifecycle helpers for the embedded Reticulum/LXMF adapter
 */

#include "chat/infra/lxmf/lxmf_link_runtime.h"

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

uint32_t lastActivityMs(const LinkSession& session)
{
    return std::max(session.last_inbound_ms, session.last_outbound_ms);
}

uint32_t ageSince(uint32_t now_ms, uint32_t then_ms)
{
    return now_ms - then_ms;
}

uint32_t activityAgeMs(const LinkSession& session, uint32_t now_ms)
{
    const uint32_t last_activity = lastActivityMs(session);
    return (last_activity == 0) ? ageSince(now_ms, session.created_ms)
                                : ageSince(now_ms, last_activity);
}

uint32_t inboundAgeMs(const LinkSession& session, uint32_t now_ms)
{
    return (session.last_inbound_ms == 0) ? ageSince(now_ms, session.created_ms)
                                          : ageSince(now_ms, session.last_inbound_ms);
}

uint32_t finalStaleTimeoutMs(const LinkSession& session,
                             const LinkRuntimeLimits& limits)
{
    const float rtt_s = std::max(0.0f, session.rtt_s);
    return session.stale_timeout_ms +
           static_cast<uint32_t>(rtt_s * 1000.0f) * limits.keepalive_timeout_factor +
           limits.stale_grace_ms;
}

} // namespace

LinkSession* findLinkSession(LinkRuntime& links,
                             const uint8_t link_id[reticulum::kTruncatedHashSize])
{
    if (!link_id)
    {
        return nullptr;
    }

    for (auto& session : links.sessions)
    {
        if (hashesEqual(session.link_id, link_id, sizeof(session.link_id)))
        {
            return &session;
        }
    }
    return nullptr;
}

LinkSession* findOpenLinkSessionByDestination(
    LinkRuntime& links,
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    LocalDestinationKind kind)
{
    if (!destination_hash)
    {
        return nullptr;
    }

    for (auto& session : links.sessions)
    {
        if (session.state != LinkState::Closed &&
            session.destination == kind &&
            hashesEqual(session.remote_destination_hash,
                        destination_hash,
                        sizeof(session.remote_destination_hash)))
        {
            return &session;
        }
    }
    return nullptr;
}

LinkSession* findActiveLinkSessionByDestination(
    LinkRuntime& links,
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    LocalDestinationKind kind)
{
    if (!destination_hash)
    {
        return nullptr;
    }

    for (auto& session : links.sessions)
    {
        if (session.state == LinkState::Active &&
            session.destination == kind &&
            hashesEqual(session.remote_destination_hash,
                        destination_hash,
                        sizeof(session.remote_destination_hash)))
        {
            return &session;
        }
    }
    return nullptr;
}

LinkSession& appendLinkSession(LinkRuntime& links, std::size_t max_link_sessions)
{
    if (max_link_sessions != 0 && links.sessions.size() >= max_link_sessions)
    {
        auto closed = std::find_if(links.sessions.begin(),
                                   links.sessions.end(),
                                   [](const LinkSession& session)
                                   {
                                       return session.state == LinkState::Closed;
                                   });
        if (closed != links.sessions.end())
        {
            links.sessions.erase(closed);
        }
        else
        {
            links.sessions.erase(links.sessions.begin());
        }
    }

    links.sessions.push_back(LinkSession{});
    return links.sessions.back();
}

bool closeLinkSession(LinkSession& session,
                      LinkCloseReason reason,
                      uint32_t now_ms)
{
    if (session.state == LinkState::Closed)
    {
        if (session.close_reason == LinkCloseReason::None)
        {
            session.close_reason = reason;
        }
        return false;
    }

    session.pending_requests.clear();
    session.deferred_payloads.clear();
    session.incoming_resources.clear();
    session.incoming_resource_assemblies.clear();
    session.outgoing_resources.clear();
    session.propagation_offer_validated = false;
    session.remote_identity_known = false;
    session.rtt_s = 0.0f;
    session.validated = false;
    session.last_keepalive_ms = 0;
    std::memset(session.local_enc_priv, 0, sizeof(session.local_enc_priv));
    std::memset(session.local_sig_priv, 0, sizeof(session.local_sig_priv));
    std::memset(session.derived_key, 0, sizeof(session.derived_key));
    std::memset(session.peer_enc_pub, 0, sizeof(session.peer_enc_pub));
    std::memset(session.peer_link_sig_pub, 0, sizeof(session.peer_link_sig_pub));
    std::memset(session.peer_identity_sig_pub, 0, sizeof(session.peer_identity_sig_pub));

    session.state = LinkState::Closed;
    session.close_reason = reason;
    session.last_inbound_ms = now_ms;
    session.last_outbound_ms = now_ms;
    return true;
}

void cullLinkSessionTables(LinkSession& session,
                           uint32_t now_ms,
                           const LinkRuntimeLimits& limits)
{
    session.pending_requests.erase(
        std::remove_if(session.pending_requests.begin(),
                       session.pending_requests.end(),
                       [now_ms, &limits](const LinkPendingRequest& request)
                       {
                           return request.created_ms == 0 ||
                                  ageSince(now_ms, request.created_ms) >
                                      limits.link_request_ttl_ms;
                       }),
        session.pending_requests.end());
}

LinkRuntimeMaintenance advanceLinkSessionLifecycle(LinkSession& session,
                                                   uint32_t now_ms,
                                                   const LinkRuntimeLimits& limits)
{
    LinkRuntimeMaintenance maintenance{};
    const uint32_t age_ms = activityAgeMs(session, now_ms);
    const uint32_t inbound_age_ms = inboundAgeMs(session, now_ms);

    if (session.state == LinkState::Pending || session.state == LinkState::Handshake)
    {
        maintenance.close_timeout = age_ms > limits.handshake_timeout_ms;
        return maintenance;
    }

    if (session.state == LinkState::Active)
    {
        const uint32_t outbound_age_ms = ageSince(now_ms, session.last_outbound_ms);
        maintenance.flush_deferred_payloads = true;
        // RNS 1.4 initiators keep the link alive when either receive or send
        // traffic has been quiet for one keepalive interval. Checking only
        // inbound traffic lets a chatty responder mask an otherwise silent
        // initiator and eventually time the responder out.
        maintenance.send_keepalive =
            session.initiator &&
            session.keepalive_interval_ms != 0 &&
            (inbound_age_ms >= session.keepalive_interval_ms ||
             outbound_age_ms >= session.keepalive_interval_ms) &&
            (session.last_keepalive_ms == 0 ||
             ageSince(now_ms, session.last_keepalive_ms) >= session.keepalive_interval_ms);

        if (session.stale_timeout_ms != 0 && inbound_age_ms >= session.stale_timeout_ms)
        {
            maintenance.marked_stale = true;
        }
        return maintenance;
    }

    if (session.state == LinkState::Stale)
    {
        maintenance.close_timeout =
            inbound_age_ms >= finalStaleTimeoutMs(session, limits);
        return maintenance;
    }

    if (session.state != LinkState::Closed)
    {
        maintenance.close_timeout =
            age_ms > limits.idle_timeout_ms ||
            ageSince(now_ms, session.created_ms) > limits.session_ttl_ms;
    }
    return maintenance;
}

void markLinkSessionStale(LinkSession& session)
{
    if (session.state == LinkState::Active)
    {
        session.state = LinkState::Stale;
    }
}

void removeExpiredLinkSessions(LinkRuntime& links,
                               uint32_t now_ms,
                               const LinkRuntimeLimits& limits)
{
    links.sessions.erase(
        std::remove_if(links.sessions.begin(),
                       links.sessions.end(),
                       [now_ms, &limits](const LinkSession& session)
                       {
                           const uint32_t age_ms = activityAgeMs(session, now_ms);

                           if (session.state == LinkState::Closed)
                           {
                               return age_ms > limits.closed_retention_ms;
                           }
                           if (session.state == LinkState::Pending ||
                               session.state == LinkState::Handshake)
                           {
                               return age_ms > limits.handshake_timeout_ms;
                           }
                           if (session.state == LinkState::Stale)
                           {
                               return age_ms > limits.idle_timeout_ms;
                           }
                           return age_ms > limits.idle_timeout_ms ||
                                  ageSince(now_ms, session.created_ms) > limits.session_ttl_ms;
                       }),
        links.sessions.end());
}

} // namespace chat::lxmf::runtime

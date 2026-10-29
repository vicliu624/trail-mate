/**
 * @file lxmf_link_runtime.h
 * @brief Link runtime lifecycle helpers for the embedded Reticulum/LXMF adapter
 */

#pragma once

#include "chat/infra/lxmf/lxmf_runtime_state.h"

#include <cstddef>
#include <cstdint>

namespace chat::lxmf::runtime
{

struct LinkRuntimeLimits
{
    std::size_t max_link_sessions = 0;
    uint32_t link_request_ttl_ms = 0;
    uint32_t handshake_timeout_ms = 0;
    uint32_t idle_timeout_ms = 0;
    uint32_t session_ttl_ms = 0;
    uint32_t stale_grace_ms = 0;
    uint32_t keepalive_timeout_factor = 0;
    uint32_t closed_retention_ms = 0;
};

struct LinkRuntimeMaintenance
{
    bool flush_deferred_payloads = false;
    bool send_keepalive = false;
    bool close_timeout = false;
    bool marked_stale = false;
};

LinkSession* findLinkSession(LinkRuntime& links,
                             const uint8_t link_id[reticulum::kTruncatedHashSize]);
LinkSession* findOpenLinkSessionByDestination(
    LinkRuntime& links,
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    LocalDestinationKind kind);
LinkSession* findActiveLinkSessionByDestination(
    LinkRuntime& links,
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    LocalDestinationKind kind);

LinkSession& appendLinkSession(LinkRuntime& links, std::size_t max_link_sessions);

bool closeLinkSession(LinkSession& session,
                      LinkCloseReason reason,
                      uint32_t now_ms);

void cullLinkSessionTables(LinkSession& session,
                           uint32_t now_ms,
                           const LinkRuntimeLimits& limits);
LinkRuntimeMaintenance advanceLinkSessionLifecycle(LinkSession& session,
                                                   uint32_t now_ms,
                                                   const LinkRuntimeLimits& limits);
void markLinkSessionStale(LinkSession& session);
void removeExpiredLinkSessions(LinkRuntime& links,
                               uint32_t now_ms,
                               const LinkRuntimeLimits& limits);

} // namespace chat::lxmf::runtime

/*
 * Reticulum License - RNS/LXMF-derived portions.
 * Derived from https://github.com/markqvist/Reticulum
 * Copyright (c) 2016-2026 Mark Qvist (Reticulum-derived portions).
 * Derived from https://github.com/markqvist/LXMF
 * Copyright (c) 2020-2026 Mark Qvist (LXMF-derived portions).
 * Full license: LICENSES/Reticulum-Derivatives.txt.
 * Original notices: LICENSES/Reticulum-RNS.txt and LICENSES/Reticulum-LXMF.txt.
 * Other contributions retain their applicable terms; see LICENSE.
 * Component scope: docs/licensing/reticulum-sources.csv.
 */

/**
 * @file lxmf_ping_service.h
 * @brief Pending Reticulum ping request owner.
 */

#pragma once

#include "chat/infra/lxmf/lxmf_wire.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_memory.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace chat::lxmf::runtime
{

struct PendingPingRequest
{
    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    uint32_t created_ms = 0;
    uint32_t last_path_request_ms = 0;
    uint32_t last_send_attempt_ms = 0;
};

using PendingPingRequestList =
    std::vector<PendingPingRequest, PsramAllocator<PendingPingRequest>>;

enum class PendingPingQueueResult
{
    Queued,
    Duplicate,
    Full,
    Invalid,
};

class PingService
{
  public:
    PingService() = default;
    PingService(const PingService&) = delete;
    PingService& operator=(const PingService&) = delete;
    PingService(PingService&&) = delete;
    PingService& operator=(PingService&&) = delete;

    std::size_t size() const;
    void clear();

    PendingPingQueueResult queue(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize],
        uint32_t now_ms,
        std::size_t max_pending);

    template <typename PeerReadyFn,
              typename DispatchFn,
              typename PathRetryFn,
              typename TimeoutFn>
    void pump(uint32_t now_ms,
              bool paused,
              uint32_t ttl_ms,
              uint32_t send_retry_ms,
              uint32_t path_retry_ms,
              PeerReadyFn&& peer_ready,
              DispatchFn&& dispatch,
              PathRetryFn&& path_retry,
              TimeoutFn&& timeout)
    {
        for (std::size_t index = 0; index < pending_.size();)
        {
            PendingPingRequest& request = pending_[index];
            const uint32_t elapsed_ms = now_ms - request.created_ms;

            if (request.created_ms == 0 || elapsed_ms > ttl_ms)
            {
                timeout(request, elapsed_ms);
                pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(index));
                continue;
            }

            if (paused)
            {
                ++index;
                continue;
            }

            const bool ready = peer_ready(request.destination_hash);
            if (ready &&
                (request.last_send_attempt_ms == 0 ||
                 (now_ms - request.last_send_attempt_ms) >= send_retry_ms))
            {
                request.last_send_attempt_ms = now_ms;
                if (dispatch(request.destination_hash,
                             request.created_ms,
                             elapsed_ms))
                {
                    pending_.erase(pending_.begin() +
                                   static_cast<std::ptrdiff_t>(index));
                    continue;
                }
            }
            else if (!ready &&
                     (request.last_path_request_ms == 0 ||
                      (now_ms - request.last_path_request_ms) >= path_retry_ms))
            {
                request.last_path_request_ms = now_ms;
                path_retry(request.destination_hash, elapsed_ms);
            }

            ++index;
        }
    }

  private:
    PendingPingRequestList pending_;
};

} // namespace chat::lxmf::runtime

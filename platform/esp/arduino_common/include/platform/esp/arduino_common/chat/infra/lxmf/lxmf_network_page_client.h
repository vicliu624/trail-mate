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
 * @file lxmf_network_page_client.h
 * @brief Pending Nomad/Network page request owner.
 */

#pragma once

#include "chat/infra/lxmf/lxmf_wire.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_memory.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace chat::lxmf::runtime
{

struct PendingNomadPageRequest
{
    static constexpr std::size_t kMaxRequestDataBytes = 256;
    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t request_id[reticulum::kTruncatedHashSize] = {};
    char path[64] = {};
    uint32_t created_ms = 0;
    uint32_t last_attempt_ms = 0;
    uint32_t last_path_request_ms = 0;
    bool path_requested = false;
    bool link_started = false;
    bool request_sent = false;
    uint8_t request_data[kMaxRequestDataBytes] = {};
    std::size_t request_data_len = 0;
};

using PendingNomadPageRequestList =
    std::vector<PendingNomadPageRequest,
                PsramAllocator<PendingNomadPageRequest>>;

enum class NetworkPageQueueResult
{
    Queued,
    Duplicate,
    Full,
    Invalid,
};

class NetworkPageClient
{
  public:
    NetworkPageClient() = default;
    NetworkPageClient(const NetworkPageClient&) = delete;
    NetworkPageClient& operator=(const NetworkPageClient&) = delete;
    NetworkPageClient(NetworkPageClient&&) = delete;
    NetworkPageClient& operator=(NetworkPageClient&&) = delete;

    std::size_t size() const;
    bool empty() const;
    void clear();

    NetworkPageQueueResult queue(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize],
        const char* path,
        const uint8_t* request_data,
        std::size_t request_data_len,
        uint32_t now_ms,
        std::size_t max_pending,
        std::size_t max_path_len,
        PendingNomadPageRequest** out_request);

    PendingNomadPageRequest* at(std::size_t index);
    const PendingNomadPageRequest* at(std::size_t index) const;
    void eraseAt(std::size_t index);
    bool erasePage(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize],
        const char* path,
        PendingNomadPageRequest* out_removed = nullptr);

    PendingNomadPageRequest* findByRequestId(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize],
        const uint8_t* request_id,
        std::size_t request_id_len);
    bool attemptDue(const PendingNomadPageRequest& request,
                    uint32_t now_ms,
                    uint32_t retry_interval_ms) const;
    bool pathRequestDue(const PendingNomadPageRequest& request,
                        uint32_t now_ms,
                        uint32_t retry_interval_ms) const;
    uint32_t lastAttemptAge(const PendingNomadPageRequest& request,
                            uint32_t now_ms) const;
    void noteAttempt(PendingNomadPageRequest& request, uint32_t now_ms);
    void notePathRequest(PendingNomadPageRequest& request,
                         bool sent,
                         uint32_t now_ms);
    void noteLinkStart(PendingNomadPageRequest& request,
                       bool sent,
                       uint32_t now_ms,
                       bool accumulate_success);
    bool noteRequestPacketSent(PendingNomadPageRequest& request,
                               const uint8_t* request_id,
                               std::size_t request_id_len,
                               uint32_t now_ms);

    template <typename Fn>
    void forEach(Fn&& fn)
    {
        for (auto& request : pending_)
        {
            fn(request);
        }
    }

    template <typename Fn>
    void forEach(Fn&& fn) const
    {
        for (const auto& request : pending_)
        {
            fn(request);
        }
    }

  private:
    PendingNomadPageRequestList pending_;
};

} // namespace chat::lxmf::runtime

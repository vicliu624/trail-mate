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
 * @file lxmf_network_page_client.cpp
 * @brief Pending Nomad/Network page request owner.
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_network_page_client.h"

#include <cstdio>
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

bool isZeroBytes(const uint8_t* data, std::size_t len)
{
    if (!data)
    {
        return true;
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        if (data[i] != 0)
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

} // namespace

std::size_t NetworkPageClient::size() const
{
    return pending_.size();
}

bool NetworkPageClient::empty() const
{
    return pending_.empty();
}

void NetworkPageClient::clear()
{
    pending_.clear();
}

NetworkPageQueueResult NetworkPageClient::queue(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    const char* path,
    const uint8_t* request_data,
    std::size_t request_data_len,
    uint32_t now_ms,
    std::size_t max_pending,
    std::size_t max_path_len,
    PendingNomadPageRequest** out_request)
{
    if (out_request)
    {
        *out_request = nullptr;
    }
    if (!destination_hash ||
        isZeroBytes(destination_hash, reticulum::kTruncatedHashSize) ||
        !path || path[0] == '\0' ||
        (request_data_len != 0 && !request_data) ||
        request_data_len > PendingNomadPageRequest::kMaxRequestDataBytes ||
        std::strlen(path) >= max_path_len ||
        max_path_len > sizeof(PendingNomadPageRequest::path))
    {
        return NetworkPageQueueResult::Invalid;
    }

    for (PendingNomadPageRequest& pending : pending_)
    {
        if (hashesEqual(pending.destination_hash,
                        destination_hash,
                        reticulum::kTruncatedHashSize) &&
            std::strcmp(pending.path, path) == 0)
        {
            if (out_request)
            {
                *out_request = &pending;
            }
            return NetworkPageQueueResult::Duplicate;
        }
    }

    if (pending_.size() >= max_pending)
    {
        return NetworkPageQueueResult::Full;
    }

    PendingNomadPageRequest request{};
    copyHash(request.destination_hash,
             destination_hash,
             sizeof(request.destination_hash));
    std::snprintf(request.path, sizeof(request.path), "%s", path);
    request.created_ms = now_ms;
    if (request_data_len != 0)
    {
        std::memcpy(request.request_data, request_data, request_data_len);
        request.request_data_len = request_data_len;
    }
    pending_.push_back(request);
    if (out_request)
    {
        *out_request = &pending_.back();
    }
    return NetworkPageQueueResult::Queued;
}

PendingNomadPageRequest* NetworkPageClient::at(std::size_t index)
{
    return index < pending_.size() ? &pending_[index] : nullptr;
}

const PendingNomadPageRequest* NetworkPageClient::at(std::size_t index) const
{
    return index < pending_.size() ? &pending_[index] : nullptr;
}

void NetworkPageClient::eraseAt(std::size_t index)
{
    if (index < pending_.size())
    {
        pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

bool NetworkPageClient::erasePage(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    const char* path,
    PendingNomadPageRequest* out_removed)
{
    if (!destination_hash || !path)
    {
        return false;
    }
    for (std::size_t index = 0; index < pending_.size(); ++index)
    {
        const PendingNomadPageRequest& request = pending_[index];
        if (hashesEqual(request.destination_hash,
                        destination_hash,
                        reticulum::kTruncatedHashSize) &&
            std::strcmp(request.path, path) == 0)
        {
            if (out_removed)
            {
                *out_removed = request;
            }
            eraseAt(index);
            return true;
        }
    }
    return false;
}

PendingNomadPageRequest* NetworkPageClient::findByRequestId(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    const uint8_t* request_id,
    std::size_t request_id_len)
{
    if (!destination_hash || !request_id ||
        request_id_len != reticulum::kTruncatedHashSize)
    {
        return nullptr;
    }
    for (PendingNomadPageRequest& request : pending_)
    {
        if (hashesEqual(request.destination_hash,
                        destination_hash,
                        reticulum::kTruncatedHashSize) &&
            std::memcmp(request.request_id,
                        request_id,
                        reticulum::kTruncatedHashSize) == 0)
        {
            return &request;
        }
    }
    return nullptr;
}

bool NetworkPageClient::attemptDue(const PendingNomadPageRequest& request,
                                   uint32_t now_ms,
                                   uint32_t retry_interval_ms) const
{
    return request.last_attempt_ms == 0 ||
           (now_ms - request.last_attempt_ms) >= retry_interval_ms;
}

bool NetworkPageClient::pathRequestDue(
    const PendingNomadPageRequest& request,
    uint32_t now_ms,
    uint32_t retry_interval_ms) const
{
    return request.last_path_request_ms == 0 ||
           (now_ms - request.last_path_request_ms) >= retry_interval_ms;
}

uint32_t NetworkPageClient::lastAttemptAge(
    const PendingNomadPageRequest& request,
    uint32_t now_ms) const
{
    return request.last_attempt_ms != 0 ? (now_ms - request.last_attempt_ms)
                                        : 0U;
}

void NetworkPageClient::noteAttempt(PendingNomadPageRequest& request,
                                    uint32_t now_ms)
{
    request.last_attempt_ms = now_ms;
}

void NetworkPageClient::notePathRequest(PendingNomadPageRequest& request,
                                        bool sent,
                                        uint32_t now_ms)
{
    request.last_path_request_ms = now_ms;
    if (sent)
    {
        request.path_requested = true;
    }
}

void NetworkPageClient::noteLinkStart(PendingNomadPageRequest& request,
                                      bool sent,
                                      uint32_t now_ms,
                                      bool accumulate_success)
{
    request.last_attempt_ms = now_ms;
    request.link_started =
        accumulate_success ? (request.link_started || sent) : sent;
}

bool NetworkPageClient::noteRequestPacketSent(
    PendingNomadPageRequest& request,
    const uint8_t* request_id,
    std::size_t request_id_len,
    uint32_t now_ms)
{
    if (!request_id || request_id_len != sizeof(request.request_id))
    {
        return false;
    }
    copyHash(request.request_id, request_id, sizeof(request.request_id));
    request.last_attempt_ms = now_ms;
    request.request_sent = true;
    return true;
}

} // namespace chat::lxmf::runtime

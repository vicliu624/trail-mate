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
 * @file lxmf_ping_service.cpp
 * @brief Pending Reticulum ping request owner.
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_ping_service.h"

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

} // namespace

std::size_t PingService::size() const
{
    return pending_.size();
}

void PingService::clear()
{
    pending_.clear();
}

PendingPingQueueResult PingService::queue(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    uint32_t now_ms,
    std::size_t max_pending)
{
    if (!destination_hash ||
        isZeroBytes(destination_hash, reticulum::kTruncatedHashSize))
    {
        return PendingPingQueueResult::Invalid;
    }

    for (const PendingPingRequest& pending : pending_)
    {
        if (hashesEqual(pending.destination_hash,
                        destination_hash,
                        sizeof(pending.destination_hash)))
        {
            return PendingPingQueueResult::Duplicate;
        }
    }

    if (pending_.size() >= max_pending)
    {
        return PendingPingQueueResult::Full;
    }

    PendingPingRequest request{};
    copyHash(request.destination_hash,
             destination_hash,
             sizeof(request.destination_hash));
    request.created_ms = now_ms;
    request.last_path_request_ms = now_ms;
    pending_.push_back(request);
    return PendingPingQueueResult::Queued;
}

} // namespace chat::lxmf::runtime

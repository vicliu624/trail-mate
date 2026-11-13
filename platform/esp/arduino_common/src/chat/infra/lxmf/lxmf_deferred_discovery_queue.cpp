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
 * @file lxmf_deferred_discovery_queue.cpp
 * @brief Deferred public discovery packet queue for the embedded LXMF runtime.
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_deferred_discovery_queue.h"

#include <cstring>

namespace chat::lxmf::runtime
{
namespace
{

bool hashesEqual(const uint8_t* lhs, const uint8_t* rhs, std::size_t len)
{
    if ((!lhs || !rhs) && len != 0)
    {
        return false;
    }
    for (std::size_t index = 0; index < len; ++index)
    {
        if (lhs[index] != rhs[index])
        {
            return false;
        }
    }
    return true;
}

} // namespace

void DeferredDiscoveryQueue::clear()
{
    queue_.clear();
    scratch_ = DeferredDiscoveryPacket{};
}

bool DeferredDiscoveryQueue::contains(
    const uint8_t packet_hash[reticulum::kFullHashSize]) const
{
    if (!packet_hash)
    {
        return false;
    }
    for (std::size_t index = 0; index < queue_.size(); ++index)
    {
        const DeferredDiscoveryPacket* queued = queue_.get(index);
        if (queued &&
            hashesEqual(queued->packet_hash,
                        packet_hash,
                        reticulum::kFullHashSize))
        {
            return true;
        }
    }
    return false;
}

bool DeferredDiscoveryQueue::push(
    const reticulum::interfaces::RxPacket& packet,
    const uint8_t packet_hash[reticulum::kFullHashSize],
    bool* out_dropped)
{
    if (out_dropped)
    {
        *out_dropped = false;
    }
    if (!packet_hash || packet.len == 0 ||
        packet.len > reticulum::kReticulumMtu)
    {
        return false;
    }

    scratch_ = DeferredDiscoveryPacket{};
    std::memcpy(scratch_.data, packet.data, packet.len);
    scratch_.len = packet.len;
    scratch_.rx_meta = packet.rx_meta;
    scratch_.interface_kind = packet.interface_kind;
    scratch_.interface_id = packet.interface_id;
    std::memcpy(scratch_.packet_hash,
                packet_hash,
                sizeof(scratch_.packet_hash));

    bool dropped = false;
    queue_.pushDropOldest(scratch_, &dropped);
    if (out_dropped)
    {
        *out_dropped = dropped;
    }
    return true;
}

bool DeferredDiscoveryQueue::pop(
    reticulum::interfaces::RxPacket* out_packet)
{
    if (!out_packet || !queue_.popOldest(&scratch_))
    {
        return false;
    }
    out_packet->len = scratch_.len;
    std::memcpy(out_packet->data, scratch_.data, scratch_.len);
    out_packet->rx_meta = scratch_.rx_meta;
    out_packet->interface_kind = scratch_.interface_kind;
    out_packet->interface_id = scratch_.interface_id;
    return true;
}

} // namespace chat::lxmf::runtime

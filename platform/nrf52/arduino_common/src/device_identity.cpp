#include "platform/nrf52/arduino_common/device_identity.h"

#include <Arduino.h>

namespace platform::nrf52::arduino_common::device_identity
{
namespace
{
constexpr ::chat::NodeId kBroadcastNodeId = 0xFFFFFFFFUL;
constexpr ::chat::NodeId kReservedNodeCount = 4;

::chat::NodeId s_resolved_self_node_id = 0;

bool isReservedNodeId(::chat::NodeId node_id)
{
    return node_id == 0 || node_id == kBroadcastNodeId || node_id < kReservedNodeCount;
}

bool nodeIdInUse(::chat::NodeId node_id, const ::chat::contacts::INodeStore* node_store)
{
    if (!node_store)
    {
        return false;
    }

    const auto& entries = node_store->getEntries();
    for (const auto& entry : entries)
    {
        if (entry.node_id == node_id)
        {
            return true;
        }
    }
    return false;
}

uint32_t mixSeed(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
}

::chat::NodeId fallbackCandidate(uint32_t seed, uint32_t attempt)
{
    uint32_t candidate = mixSeed(seed + attempt * 0x9e3779b9U);
    candidate &= 0x7FFFFFFFU;
    if (candidate < kReservedNodeCount)
    {
        candidate += kReservedNodeCount;
    }
    if (candidate == kBroadcastNodeId)
    {
        candidate ^= 0x00A5A5A5U;
    }
    if (candidate < kReservedNodeCount)
    {
        candidate = kReservedNodeCount;
    }
    return candidate;
}
} // namespace

::chat::NodeId deriveNodeIdFromDeviceAddress(uint32_t deviceaddr0, uint32_t deviceaddr1)
{
    ::chat::NodeId node_id = (static_cast<::chat::NodeId>((deviceaddr0 >> 16) & 0xFFU) << 24) |
                             (static_cast<::chat::NodeId>((deviceaddr0 >> 24) & 0xFFU) << 16) |
                             (static_cast<::chat::NodeId>(deviceaddr1 & 0xFFU) << 8) |
                             static_cast<::chat::NodeId>((deviceaddr1 >> 8) & 0xFFU);
    if (node_id == 0)
    {
        node_id = 1;
    }
    return node_id;
}

std::array<uint8_t, 6> deriveMacAddressFromDeviceAddress(uint32_t deviceaddr0, uint32_t deviceaddr1)
{
    return {
        static_cast<uint8_t>(deviceaddr0 & 0xFFU),
        static_cast<uint8_t>((deviceaddr0 >> 8) & 0xFFU),
        static_cast<uint8_t>((deviceaddr0 >> 16) & 0xFFU),
        static_cast<uint8_t>((deviceaddr0 >> 24) & 0xFFU),
        static_cast<uint8_t>(deviceaddr1 & 0xFFU),
        static_cast<uint8_t>((deviceaddr1 >> 8) & 0xFFU)};
}

::chat::NodeId resolveNodeId(uint32_t deviceaddr0, uint32_t deviceaddr1,
                             uint32_t deviceid0, uint32_t deviceid1,
                             const ::chat::contacts::INodeStore* node_store)
{
    ::chat::NodeId node_id = deriveNodeIdFromDeviceAddress(deviceaddr0, deviceaddr1);
    if (!isReservedNodeId(node_id) && !nodeIdInUse(node_id, node_store))
    {
        return node_id;
    }

    const uint32_t seed = mixSeed(deviceaddr0 ^ (deviceaddr1 * 33U) ^ deviceid0 ^ (deviceid1 * 97U));
    for (uint32_t attempt = 0; attempt < 1024; ++attempt)
    {
        const ::chat::NodeId candidate = fallbackCandidate(seed, attempt);
        if (!isReservedNodeId(candidate) && !nodeIdInUse(candidate, node_store))
        {
            return candidate;
        }
    }

    return node_id == 0 ? kReservedNodeCount : node_id;
}

void setResolvedSelfNodeId(::chat::NodeId node_id)
{
    s_resolved_self_node_id = node_id;
}

void clearResolvedSelfNodeId()
{
    s_resolved_self_node_id = 0;
}

::chat::NodeId getSelfNodeId()
{
    if (s_resolved_self_node_id != 0)
    {
        return s_resolved_self_node_id;
    }
    return deriveNodeIdFromDeviceAddress(NRF_FICR->DEVICEADDR[0], NRF_FICR->DEVICEADDR[1]);
}

std::array<uint8_t, 6> getSelfMacAddress()
{
    return deriveMacAddressFromDeviceAddress(NRF_FICR->DEVICEADDR[0], NRF_FICR->DEVICEADDR[1]);
}

} // namespace platform::nrf52::arduino_common::device_identity

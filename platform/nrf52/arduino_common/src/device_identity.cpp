#include "platform/nrf52/arduino_common/device_identity.h"

#include <Arduino.h>

namespace platform::nrf52::arduino_common::device_identity
{

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

::chat::NodeId getSelfNodeId()
{
    return deriveNodeIdFromDeviceAddress(NRF_FICR->DEVICEADDR[0], NRF_FICR->DEVICEADDR[1]);
}

std::array<uint8_t, 6> getSelfMacAddress()
{
    return deriveMacAddressFromDeviceAddress(NRF_FICR->DEVICEADDR[0], NRF_FICR->DEVICEADDR[1]);
}

} // namespace platform::nrf52::arduino_common::device_identity

#pragma once

#include "chat/domain/chat_types.h"

#include <array>
#include <cstdint>

namespace platform::nrf52::arduino_common::device_identity
{

::chat::NodeId deriveNodeIdFromDeviceAddress(uint32_t deviceaddr0, uint32_t deviceaddr1);
std::array<uint8_t, 6> deriveMacAddressFromDeviceAddress(uint32_t deviceaddr0, uint32_t deviceaddr1);
::chat::NodeId getSelfNodeId();
std::array<uint8_t, 6> getSelfMacAddress();

} // namespace platform::nrf52::arduino_common::device_identity

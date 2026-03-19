#pragma once

#include "chat/domain/chat_types.h"
#include "chat/ports/i_node_store.h"

#include <array>
#include <cstdint>

namespace platform::nrf52::arduino_common::device_identity
{

::chat::NodeId deriveNodeIdFromDeviceAddress(uint32_t deviceaddr0, uint32_t deviceaddr1);
std::array<uint8_t, 6> deriveMacAddressFromDeviceAddress(uint32_t deviceaddr0, uint32_t deviceaddr1);
::chat::NodeId resolveNodeId(uint32_t deviceaddr0, uint32_t deviceaddr1,
                             uint32_t deviceid0, uint32_t deviceid1,
                             const ::chat::contacts::INodeStore* node_store);
void setResolvedSelfNodeId(::chat::NodeId node_id);
void clearResolvedSelfNodeId();
::chat::NodeId getSelfNodeId();
std::array<uint8_t, 6> getSelfMacAddress();

} // namespace platform::nrf52::arduino_common::device_identity

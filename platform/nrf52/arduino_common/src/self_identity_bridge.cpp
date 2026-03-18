#include "platform/nrf52/arduino_common/self_identity_bridge.h"

#include "platform/nrf52/arduino_common/device_identity.h"

namespace platform::nrf52::arduino_common
{

SelfIdentityBridge::SelfIdentityBridge(const app::AppConfig& config,
                                       uint32_t deviceaddr0,
                                       uint32_t deviceaddr1,
                                       const char* fallback_long_prefix,
                                       const char* fallback_ble_prefix)
    : config_(config),
      node_id_(device_identity::deriveNodeIdFromDeviceAddress(deviceaddr0, deviceaddr1)),
      mac_addr_(device_identity::deriveMacAddressFromDeviceAddress(deviceaddr0, deviceaddr1)),
      fallback_long_prefix_(fallback_long_prefix),
      fallback_ble_prefix_(fallback_ble_prefix)
{
}

bool SelfIdentityBridge::readSelfIdentityInput(chat::runtime::SelfIdentityInput* out) const
{
    if (!out)
    {
        return false;
    }

    *out = chat::runtime::SelfIdentityInput{};
    out->node_id = node_id_;
    out->configured_long_name = config_.node_name;
    out->configured_short_name = config_.short_name;
    out->fallback_long_prefix = fallback_long_prefix_;
    out->fallback_ble_prefix = fallback_ble_prefix_;
    out->allow_short_hex_fallback = true;
    out->mac_addr = mac_addr_.data();
    out->mac_addr_len = mac_addr_.size();
    return true;
}

} // namespace platform::nrf52::arduino_common

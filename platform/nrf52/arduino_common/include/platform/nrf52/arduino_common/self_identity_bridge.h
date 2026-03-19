#pragma once

#include "app/app_config.h"
#include "chat/runtime/self_identity_provider.h"

#include <array>

namespace platform::nrf52::arduino_common
{

class SelfIdentityBridge final : public ::chat::runtime::SelfIdentityProvider
{
  public:
    SelfIdentityBridge(const ::app::AppConfig& config,
                       uint32_t deviceaddr0,
                       uint32_t deviceaddr1,
                       const char* fallback_long_prefix,
                       const char* fallback_ble_prefix);

    bool readSelfIdentityInput(::chat::runtime::SelfIdentityInput* out) const override;
    void setNodeId(::chat::NodeId node_id)
    {
        node_id_ = node_id;
    }

    const std::array<uint8_t, 6>& macAddress() const
    {
        return mac_addr_;
    }

  private:
    const ::app::AppConfig& config_;
    ::chat::NodeId node_id_ = 0;
    std::array<uint8_t, 6> mac_addr_{};
    const char* fallback_long_prefix_ = nullptr;
    const char* fallback_ble_prefix_ = nullptr;
};

} // namespace platform::nrf52::arduino_common

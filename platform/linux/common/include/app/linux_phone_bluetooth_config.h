#pragma once
#include "phone/meshtastic/meshtastic_phone_core.h"
#include <functional>
#include <string>

namespace trailmate::linux_app
{
// Application-thread, bounded protobuf persistence. Invalid/unavailable storage
// disables phone readiness; only an absent record permits the shared defaults.
class LinuxPhoneBluetoothConfig final : public phone::meshtastic::MeshtasticPhoneBluetoothConfigHooks
{
  public:
    explicit LinuxPhoneBluetoothConfig(std::function<bool()> enabled);
    bool ready() const { return ready_; }
    const std::string& error() const { return error_; }
    const meshtastic_Config_BluetoothConfig& value() const { return config_; }
    uint64_t revision() const { return revision_; }
    bool loadBluetoothConfig(meshtastic_Config_BluetoothConfig* out) const override;
    void saveBluetoothConfig(const meshtastic_Config_BluetoothConfig&) override;

  private:
    std::function<bool()> enabled_;
    meshtastic_Config_BluetoothConfig config_ = meshtastic_Config_BluetoothConfig_init_zero;
    bool ready_ = false;
    uint64_t revision_ = 0;
    std::string error_;
};
} // namespace trailmate::linux_app

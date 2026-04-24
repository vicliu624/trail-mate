#pragma once

#include "chat/ble/meshtastic_phone_core.h"

namespace ble
{

class MeshtasticPhoneSession
{
  public:
    MeshtasticPhoneSession(IPhoneRuntimeContext& ctx,
                           MeshtasticPhoneTransport& transport,
                           MeshtasticPhoneBluetoothConfigHooks* bluetooth_config_hooks,
                           MeshtasticPhoneModuleConfigHooks* module_config_hooks,
                           MeshtasticPhoneConfigLifecycleHooks* config_lifecycle_hooks,
                           MeshtasticPhoneStatusHooks* status_hooks,
                           MeshtasticPhoneMqttHooks* mqtt_hooks,
                           MeshtasticPhoneDeviceRuntimeHooks* device_runtime_hooks);

    void close();
    void pumpIncomingAppData();
    bool isSendingPackets() const;
    bool isConfigFlowActive() const;
    bool handleToRadio(const uint8_t* buf, size_t len);
    bool popToPhone(MeshtasticBleFrame* out);
    void onIncomingText(const chat::MeshIncomingText& msg);
    void onIncomingData(const chat::MeshIncomingData& msg);

  private:
    MeshtasticPhoneCore core_;
};

} // namespace ble

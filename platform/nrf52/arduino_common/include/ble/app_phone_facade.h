#pragma once

#include "app/app_facades.h"
#include "phone/common/phone_runtime_context.h"
#include "phone/meshcore/meshcore_phone_core.h"
#include "phone/meshtastic/meshtastic_phone_core.h"
#include "platform/shared/ble/phone_ble_runtime.h"

#include <string>

namespace ble
{

class AppPhoneFacade final : public phone::IPhoneRuntimeContext,
                             public phone::meshtastic::MeshtasticPhoneBluetoothConfigHooks,
                             public phone::meshtastic::MeshtasticPhoneModuleConfigHooks,
                             public phone::meshtastic::MeshtasticPhoneConfigLifecycleHooks,
                             public phone::meshtastic::MeshtasticPhoneStatusHooks,
                             public phone::meshtastic::MeshtasticPhoneMqttHooks,
                             public phone::meshtastic::MeshtasticPhoneDeviceRuntimeHooks,
                             public phone::meshcore::MeshCorePhoneHooks
{
  public:
    AppPhoneFacade(app::IAppBleFacade& app,
                   meshtastic_Config_BluetoothConfig& bluetooth_config,
                   meshtastic_LocalModuleConfig& module_config,
                   platform::shared::ble_bridge::IPhoneBleRuntime* ble_runtime = nullptr);

    void setBleRuntime(platform::shared::ble_bridge::IPhoneBleRuntime* runtime);

    phone::MeshtasticPhoneConfigSnapshot getMeshtasticPhoneConfig() const override;
    void setMeshtasticPhoneConfig(const phone::MeshtasticPhoneConfigSnapshot& config) override;
    phone::MeshCorePhoneConfigSnapshot getMeshCorePhoneConfig() const override;
    void setMeshCorePhoneConfig(const phone::MeshCorePhoneConfigSnapshot& config) override;
    void saveConfig() override;
    void applyMeshConfig() override;
    void applyUserInfo() override;
    void applyPositionConfig() override;
    void getEffectiveUserInfo(char* out_long,
                              std::size_t long_len,
                              char* out_short,
                              std::size_t short_len) const override;
    chat::ChatService& getChatService() override;
    chat::contacts::ContactService& getContactService() override;
    chat::IMeshAdapter* getMeshAdapter() override;
    const chat::IMeshAdapter* getMeshAdapter() const override;
    chat::contacts::INodeStore* getNodeStore() override;
    const chat::contacts::INodeStore* getNodeStore() const override;
    chat::NodeId getSelfNodeId() const override;
    bool isBleEnabled() const override;
    void setBleEnabled(bool enabled) override;
    bool getDeviceMacAddress(uint8_t out_mac[6]) const override;
    bool syncCurrentEpochSeconds(uint32_t epoch_seconds) override;
    void resetMeshConfig() override;
    void clearNodeDb() override;
    void clearMessageDb() override;
    void restartDevice() override;

    bool loadBluetoothConfig(meshtastic_Config_BluetoothConfig* out) const override;
    void saveBluetoothConfig(const meshtastic_Config_BluetoothConfig& config) override;
    bool loadModuleConfig(meshtastic_LocalModuleConfig* out) const override;
    void saveModuleConfig(const meshtastic_LocalModuleConfig& config) override;
    void onConfigStart() override;
    void onConfigComplete() override;
    bool loadDeviceConnectionStatus(meshtastic_DeviceConnectionStatus* out) const override;
    bool handleMqttProxyToRadio(const meshtastic_MqttClientProxyMessage& msg) override;
    bool pollMqttProxyToPhone(meshtastic_MqttClientProxyMessage* out) override;
    bool loadTimezoneTzdef(char* out, size_t out_len) const override;
    void saveTimezoneTzdef(const char* tzdef) override;
    int getTimezoneOffsetMinutes() const override;
    void setTimezoneOffsetMinutes(int offset_min) override;
    bool getGpsFix(phone::meshtastic::MeshtasticGpsFix* out) const override;

    bool getCustomVars(std::string* out) const override;
    bool setCustomVar(const char* key, const char* value) override;

  private:
    app::IAppBleFacade& app_;
    meshtastic_Config_BluetoothConfig& bluetooth_config_;
    meshtastic_LocalModuleConfig& module_config_;
    platform::shared::ble_bridge::IPhoneBleRuntime* ble_runtime_ = nullptr;
};

} // namespace ble

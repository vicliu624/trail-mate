#pragma once

#include "app/app_facades.h"
#include "phone/common/phone_app_facade.h"
#include "phone/meshcore/meshcore_phone_core.h"
#include "phone/meshtastic/meshtastic_phone_core.h"
#include "platform/shared/ble/phone_ble_runtime.h"

#include <cstddef>
#include <string>

namespace chat::meshcore
{
class IMeshCoreBleBackend;
}

namespace ble
{

class AppPhoneFacade final : public phone::IPhoneAppFacade,
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
    void syncMeshtasticMqttProxySettings(const meshtastic_LocalModuleConfig& module_config);

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
    chat::NodeId getSelfNodeId() const override;
    bool sendPhoneText(chat::ChannelId channel,
                       const std::string& text,
                       chat::MessageId forced_msg_id,
                       chat::NodeId peer,
                       chat::MessageId& out_msg_id) override;
    bool sendPhoneAppData(chat::ChannelId channel,
                          uint32_t portnum,
                          const uint8_t* payload,
                          std::size_t len,
                          chat::NodeId dest,
                          bool want_ack,
                          chat::MessageId packet_id,
                          bool want_response) override;
    bool pollIncomingPhoneData(chat::MeshIncomingData& out) override;
    std::size_t phoneNodeCount() const override;
    bool getPhoneNodeByIndex(std::size_t index, phone::PhoneNodeView& out) const override;
    bool findPhoneNode(chat::NodeId node_id, phone::PhoneNodeView& out) const override;
    bool isMeshPkiReady() const override;
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
    bool meshCoreExportIdentityPublicKey(uint8_t* out_pubkey, std::size_t len) override;
    bool meshCoreExportIdentityPrivateKey(uint8_t* out_priv, std::size_t len) override;
    bool meshCoreImportIdentityPrivateKey(const uint8_t* in_priv, std::size_t len) override;
    bool meshCoreSignPayload(const uint8_t* data, std::size_t len, uint8_t* out_sig, std::size_t out_len) override;
    bool meshCoreSendSelfAdvert(bool broadcast) override;
    bool meshCoreSendPeerRequestType(const uint8_t* pubkey,
                                     std::size_t len,
                                     uint8_t req_type,
                                     uint32_t* out_tag,
                                     uint32_t* out_est_timeout,
                                     bool* out_sent_flood) override;
    bool meshCoreSendPeerRequestPayload(const uint8_t* pubkey,
                                        std::size_t len,
                                        const uint8_t* payload,
                                        std::size_t payload_len,
                                        bool force_flood,
                                        uint32_t* out_tag,
                                        uint32_t* out_est_timeout,
                                        bool* out_sent_flood) override;
    bool meshCoreSendAnonRequestPayload(const uint8_t* pubkey,
                                        std::size_t len,
                                        const uint8_t* payload,
                                        std::size_t payload_len,
                                        uint32_t* out_est_timeout,
                                        bool* out_sent_flood) override;
    bool meshCoreSendTracePath(const uint8_t* path,
                               std::size_t path_len,
                               uint32_t tag,
                               uint32_t auth,
                               uint8_t flags,
                               uint32_t* out_est_timeout) override;
    bool meshCoreSendControlData(const uint8_t* payload, std::size_t payload_len) override;
    bool meshCoreSendRawData(const uint8_t* path,
                             std::size_t path_len,
                             const uint8_t* payload,
                             std::size_t payload_len,
                             uint32_t* out_est_timeout) override;
    void meshCoreSetFloodScopeKey(const uint8_t* key, std::size_t len) override;

  private:
    chat::meshcore::IMeshCoreBleBackend* meshCoreBackend();
    const chat::meshcore::IMeshCoreBleBackend* meshCoreBackend() const;

    app::IAppBleFacade& app_;
    meshtastic_Config_BluetoothConfig& bluetooth_config_;
    meshtastic_LocalModuleConfig& module_config_;
    platform::shared::ble_bridge::IPhoneBleRuntime* ble_runtime_ = nullptr;
};

} // namespace ble

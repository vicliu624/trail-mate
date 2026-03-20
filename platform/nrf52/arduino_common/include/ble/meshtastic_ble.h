#pragma once

#include "app/app_facades.h"
#include "ble_manager.h"
#include "chat/ble/meshtastic_phone_session.h"
#include "chat/domain/chat_types.h"
#include "chat/ports/i_node_store.h"
#include "chat/usecase/chat_service.h"
#include "meshtastic/admin.pb.h"
#include "meshtastic/channel.pb.h"
#include "meshtastic/config.pb.h"
#include "meshtastic/device_ui.pb.h"
#include "meshtastic/localonly.pb.h"
#include "meshtastic/mesh.pb.h"
#include "meshtastic/module_config.pb.h"
#include "meshtastic/telemetry.pb.h"
#include <atomic>
#include <bluefruit.h>
#include <deque>
#include <memory>
#include <string>

namespace ble
{

class MeshtasticPhoneCore;

class MeshtasticBleService final : public BleService,
                                   public chat::ChatService::IncomingTextObserver,
                                   public MeshtasticPhoneTransport,
                                   public MeshtasticPhoneHooks
{
  public:
    MeshtasticBleService(app::IAppBleFacade& ctx, const std::string& device_name);
    ~MeshtasticBleService() override;

    void start() override;
    void stop() override;
    void update() override;
    void onIncomingText(const chat::MeshIncomingText& msg) override;
    bool handleToRadio(const uint8_t* data, size_t len);
    bool popToPhone(MeshtasticBleFrame* out);
    void handleConnectEvent(uint16_t conn_handle);
    void handleDisconnectEvent(uint16_t conn_handle);
    void handlePairPasskeyDisplay(uint16_t conn_handle, const uint8_t passkey[6], bool match_request);
    void handlePairComplete(uint16_t conn_handle, uint8_t auth_status);
    void handleSecured(uint16_t conn_handle);
    bool getPairingStatus(BlePairingStatus* out) const override;

    bool isBleConnected() const override;
    void notifyFromNum(uint32_t from_num) override;
    bool loadBluetoothConfig(meshtastic_Config_BluetoothConfig* out) const override;
    bool loadDeviceConnectionStatus(meshtastic_DeviceConnectionStatus* out) const override;
    bool loadModuleConfig(meshtastic_LocalModuleConfig* out) const override;
    void saveModuleConfig(const meshtastic_LocalModuleConfig& config) override;
    bool handleMqttProxyToRadio(const meshtastic_MqttClientProxyMessage& msg) override;
    bool pollMqttProxyToPhone(meshtastic_MqttClientProxyMessage* out) override;

  private:
    void syncMqttProxySettings();
    void applyBleSecurity();
    void requestPairingIfNeeded(uint16_t conn_handle);
    uint32_t effectivePasskey() const;

    app::IAppBleFacade& ctx_;
    std::string device_name_;
    ::BLEService service_;
    ::BLECharacteristic to_radio_;
    ::BLECharacteristic from_radio_;
    ::BLECharacteristic from_num_;
    ::BLECharacteristic log_radio_;
    bool active_ = false;
    bool connected_ = false;
    meshtastic_Config_BluetoothConfig ble_config_ = meshtastic_Config_BluetoothConfig_init_zero;
    meshtastic_LocalModuleConfig module_config_ = meshtastic_LocalModuleConfig_init_zero;
    std::unique_ptr<MeshtasticPhoneSession> phone_session_;
    std::atomic<uint32_t> pending_passkey_{0};
};

} // namespace ble

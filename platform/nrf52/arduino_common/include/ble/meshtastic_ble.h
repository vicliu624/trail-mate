#pragma once

#include "app/app_facades.h"
#include "ble/ble_manager.h"
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
#include <array>
#include <atomic>
#include <bluefruit.h>
#include <memory>
#include <string>

namespace ble
{

class MeshtasticPhoneCore;

class MeshtasticBleService final : public BleService,
                                   public chat::ChatService::IncomingTextObserver,
                                   public chat::ChatService::OutgoingTextObserver,
                                   public chat::ChatService::IncomingDataObserver,
                                   public MeshtasticPhoneTransport,
                                   public MeshtasticPhoneHooks
{
  public:
    struct Frame
    {
        size_t len = 0;
        uint32_t from_num = 0;
        std::array<uint8_t, meshtastic_FromRadio_size> buf{};
    };

    MeshtasticBleService(app::IAppBleFacade& ctx, const std::string& device_name);
    ~MeshtasticBleService() override;

    void start() override;
    void stop() override;
    void update() override;
    bool isRunning() const override;
    void setDeviceName(const std::string& name) override;

    void onIncomingText(const chat::MeshIncomingText& msg) override;
    void onOutgoingText(const chat::MeshIncomingText& msg) override;
    void onIncomingData(const chat::MeshIncomingData& msg) override;

    bool handleToRadio(const uint8_t* data, size_t len);
    bool enqueueToRadio(const uint8_t* data, size_t len);
    void handleToPhone();
    void flushPendingFromNumNotify();
    bool shouldBlockOnRead() const;
    bool hasReadableFromRadio() const;
    void markReadableFromRadioConsumed();
    bool popQueuedToPhoneFrame(Frame* out);
    void beginReadWait();
    bool isReadWaiting() const;
    void endReadWait();
    bool waitForReadableFromRadio(uint8_t max_tries, uint8_t delay_ms);
    void handleConnectEvent(uint16_t conn_handle);
    void handleDisconnectEvent(uint16_t conn_handle);
    void handleFromNumCccdWrite(uint16_t conn_handle, uint16_t value);
    void handlePairPasskeyDisplay(uint16_t conn_handle, const uint8_t passkey[6], bool match_request);
    void handlePairComplete(uint16_t conn_handle, uint8_t auth_status);
    void handleSecured(uint16_t conn_handle);

    bool getPairingStatus(BlePairingStatus* out) const override;
    bool isBleConnected() const override;
    void notifyFromNum(uint32_t from_num) override;
    bool loadBluetoothConfig(meshtastic_Config_BluetoothConfig* out) const override;
    void saveBluetoothConfig(const meshtastic_Config_BluetoothConfig& config) override;
    bool loadDeviceConnectionStatus(meshtastic_DeviceConnectionStatus* out) const override;
    bool loadModuleConfig(meshtastic_LocalModuleConfig* out) const override;
    void saveModuleConfig(const meshtastic_LocalModuleConfig& config) override;
    bool handleMqttProxyToRadio(const meshtastic_MqttClientProxyMessage& msg) override;
    bool pollMqttProxyToPhone(meshtastic_MqttClientProxyMessage* out) override;

  private:
    struct PendingToRadioFrame
    {
        size_t len = 0;
        uint8_t buf[meshtastic_ToRadio_size] = {};
    };

    void processPendingToRadio();
    void processPendingPairingRequest();
    bool enqueueToPhoneFrame(const Frame& frame);
    void clearToPhoneQueue();
    void prepareReadableFromRadio();
    void syncMqttProxySettings();
    void markConfigSavePending(bool bluetooth_changed, bool module_changed);
    void flushPendingConfigSaves(bool force = false);
    void applyBleSecurity();
    void logFromRadioState(const char* tag) const;
    void requestPairingIfNeeded(uint16_t conn_handle);
    uint32_t effectivePasskey() const;
    void logDeferredBleEvents();

    app::IAppBleFacade& ctx_;
    std::string device_name_;
    ::BLEService service_;
    ::BLECharacteristic to_radio_;
    ::BLECharacteristic from_radio_;
    ::BLECharacteristic from_num_;
    ::BLECharacteristic log_radio_;
    bool active_ = false;
    bool gatt_initialized_ = false;
    bool observers_registered_ = false;
    bool connected_ = false;
    bool from_num_notify_enabled_ = false;
    uint16_t conn_handle_ = BLE_CONN_HANDLE_INVALID;
    meshtastic_Config_BluetoothConfig ble_config_ = meshtastic_Config_BluetoothConfig_init_zero;
    meshtastic_LocalModuleConfig module_config_ = meshtastic_LocalModuleConfig_init_zero;
    std::unique_ptr<MeshtasticPhoneSession> phone_session_;
    std::atomic<uint32_t> pending_passkey_{0};

    static constexpr uint8_t kPendingToRadioCapacity = 6;
    PendingToRadioFrame pending_to_radio_[kPendingToRadioCapacity]{};
    volatile uint8_t pending_to_radio_head_ = 0;
    volatile uint8_t pending_to_radio_tail_ = 0;
    volatile uint8_t pending_to_radio_count_ = 0;

    volatile bool pairing_request_pending_ = false;
    volatile uint16_t pending_pairing_conn_handle_ = BLE_CONN_HANDLE_INVALID;

    std::atomic<bool> read_waiting_{false};

    bool pending_to_phone_valid_ = false;
    Frame pending_to_phone_{};
    MeshtasticBleFrame session_frame_scratch_{};

    static constexpr uint8_t kToPhoneQueueDepth = 3;
    Frame to_phone_queue_[kToPhoneQueueDepth]{};
    volatile uint8_t to_phone_head_ = 0;
    volatile uint8_t to_phone_tail_ = 0;
    volatile uint8_t to_phone_count_ = 0;

    bool from_radio_preloaded_valid_ = false;
    Frame from_radio_preloaded_{};
    bool from_radio_consume_pending_ = false;

    bool pending_from_num_valid_ = false;
    uint32_t pending_from_num_ = 0;

    volatile bool pending_connect_log_ = false;
    volatile bool pending_disconnect_log_ = false;
    volatile bool pending_from_num_cccd_log_ = false;
    volatile bool pending_pair_complete_log_ = false;
    volatile bool pending_secured_log_ = false;

    volatile uint16_t pending_connect_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    volatile uint16_t pending_disconnect_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    volatile uint16_t pending_from_num_cccd_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    volatile uint16_t pending_from_num_cccd_value_ = 0;
    volatile uint16_t pending_pair_complete_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    volatile uint8_t pending_pair_complete_status_ = 0;
    volatile uint16_t pending_secured_conn_handle_ = BLE_CONN_HANDLE_INVALID;

    volatile bool pending_from_radio_read_log_ = false;
    volatile bool pending_from_radio_empty_log_ = false;
    volatile uint32_t pending_from_radio_read_from_num_ = 0;
    volatile uint16_t pending_from_radio_read_len_ = 0;

    bool bluetooth_config_save_pending_ = false;
    bool module_config_save_pending_ = false;
    uint32_t config_save_due_ms_ = 0;
    uint32_t last_ble_activity_ms_ = 0;
};

} // namespace ble

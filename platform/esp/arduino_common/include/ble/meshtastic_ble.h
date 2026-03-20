#pragma once

#include "app/app_facades.h"
#include "ble/ble_manager.h"
#include "chat/ble/meshtastic_phone_session.h"
#include "chat/infra/meshtastic/mt_codec_pb.h"
#include "chat/ports/i_node_store.h"
#include "chat/usecase/chat_service.h"
#include "meshtastic/admin.pb.h"
#include "meshtastic/channel.pb.h"
#include "meshtastic/config.pb.h"
#include "meshtastic/device_ui.pb.h"
#include "meshtastic/localonly.pb.h"
#include "meshtastic/mesh.pb.h"
#include "meshtastic/module_config.pb.h"
#include "team/usecase/team_service.h"
#include <NimBLEDevice.h>
#include <array>
#include <atomic>
#include <deque>
#include <string>
#include <vector>

namespace ble
{

class MeshtasticBleService : public BleService,
                             public chat::ChatService::IncomingTextObserver,
                             public team::TeamService::IncomingDataObserver,
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
    void onIncomingData(const chat::MeshIncomingData& msg) override;
    bool isBleConnected() const override;
    void notifyFromNum(uint32_t value) override;
    bool loadBluetoothConfig(meshtastic_Config_BluetoothConfig* out) const override;
    void saveBluetoothConfig(const meshtastic_Config_BluetoothConfig& config) override;
    bool loadDeviceConnectionStatus(meshtastic_DeviceConnectionStatus* out) const override;
    bool loadModuleConfig(meshtastic_LocalModuleConfig* out) const override;
    void saveModuleConfig(const meshtastic_LocalModuleConfig& config) override;
    bool handleMqttProxyToRadio(const meshtastic_MqttClientProxyMessage& msg) override;
    bool pollMqttProxyToPhone(meshtastic_MqttClientProxyMessage* out) override;
    void onConfigStart() override;
    void onConfigComplete() override;

  private:
    app::IAppBleFacade& ctx_;
    std::string device_name_;
    NimBLEServer* server_ = nullptr;
    NimBLEService* service_ = nullptr;
    NimBLEService* battery_service_ = nullptr;
    NimBLECharacteristic* to_radio_ = nullptr;
    NimBLECharacteristic* from_radio_ = nullptr;
    NimBLECharacteristic* from_radio_sync_ = nullptr;
    NimBLECharacteristic* from_num_ = nullptr;
    NimBLECharacteristic* log_radio_ = nullptr;
    NimBLECharacteristic* battery_level_ = nullptr;
    bool connected_ = false;
    uint16_t conn_handle_ = 0;
    bool conn_handle_valid_ = false;
    bool from_num_subscribed_ = false;
    bool from_radio_sync_subscribed_ = false;
    uint16_t from_radio_sync_sub_value_ = 0;
    std::atomic<uint32_t> pending_passkey_{0};
    int last_battery_level_ = -2;

    meshtastic_Config_BluetoothConfig ble_config_ = meshtastic_Config_BluetoothConfig_init_zero;
    meshtastic_LocalModuleConfig module_config_ = meshtastic_LocalModuleConfig_init_zero;

    std::unique_ptr<MeshtasticPhoneSession> phone_session_;

    struct Frame
    {
        size_t len = 0;
        std::array<uint8_t, meshtastic_FromRadio_size> buf{};
    };

    // Keep queue depths aligned with official NimBLE transport behavior.
    static constexpr size_t kFromPhoneQueueDepth = 3;
    static constexpr size_t kToPhoneQueueDepth = 3;

    std::array<NimBLEAttValue, kFromPhoneQueueDepth> from_phone_queue_{};
    size_t from_phone_len_ = 0;
    std::array<uint8_t, meshtastic_ToRadio_size> last_to_radio_{};
    size_t last_to_radio_len_ = 0;

    std::array<Frame, kToPhoneQueueDepth> to_phone_queue_{};
    size_t to_phone_len_ = 0;

    std::atomic<bool> read_waiting_{false};
    bool pending_to_phone_valid_ = false;
    Frame pending_to_phone_{};
    uint32_t pending_to_phone_from_num_ = 0;

    SemaphoreHandle_t from_phone_mutex_ = nullptr;
    SemaphoreHandle_t to_phone_mutex_ = nullptr;

    void loadBleConfig();
    void saveBleConfig();
    void applyBleSecurity();
    void loadModuleConfig();
    void saveModuleConfig();
    const meshtastic_Config_BluetoothConfig& bluetoothConfig() const
    {
        return ble_config_;
    }
    const meshtastic_LocalModuleConfig& moduleConfig() const
    {
        return module_config_;
    }

    void setupService();
    void startAdvertising();
    void requestHighThroughputConnection();
    void requestLowerPowerConnection();
    void handleFromPhone();
    void handleToPhone();
    void syncMqttProxySettings();
    bool shouldBlockOnRead() const;
    void clearQueues();
    void closePhoneSession();
    void refreshBatteryLevel(bool notify);

    friend class MeshtasticToRadioCallbacks;
    friend class MeshtasticFromRadioCallbacks;
    friend class MeshtasticNotifyStateCallbacks;
    friend class MeshtasticServerCallbacks;
};

} // namespace ble

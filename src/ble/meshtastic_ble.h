#pragma once

#include "../app/app_context.h"
#include "../chat/infra/meshtastic/generated/meshtastic/admin.pb.h"
#include "../chat/infra/meshtastic/generated/meshtastic/channel.pb.h"
#include "../chat/infra/meshtastic/generated/meshtastic/config.pb.h"
#include "../chat/infra/meshtastic/generated/meshtastic/device_ui.pb.h"
#include "../chat/infra/meshtastic/generated/meshtastic/localonly.pb.h"
#include "../chat/infra/meshtastic/generated/meshtastic/mesh.pb.h"
#include "../chat/infra/meshtastic/generated/meshtastic/module_config.pb.h"
#include "../chat/infra/meshtastic/mt_codec_pb.h"
#include "../chat/usecase/chat_service.h"
#include "../team/usecase/team_service.h"
#include "ble_manager.h"
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
                             public team::TeamService::IncomingDataObserver
{
  public:
    MeshtasticBleService(app::AppContext& ctx, const std::string& device_name);
    ~MeshtasticBleService() override;

    void start() override;
    void stop() override;
    void update() override;

    void onIncomingText(const chat::MeshIncomingText& msg) override;
    void onIncomingData(const chat::MeshIncomingData& msg) override;

  private:
    class PhoneApi;

    app::AppContext& ctx_;
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

    std::unique_ptr<PhoneApi> phone_api_;

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
    void pumpMqttProxyMessages();
    bool shouldBlockOnRead() const;
    void notifyFromNum(uint32_t value);
    void clearQueues();
    void closePhoneApi();
    void refreshBatteryLevel(bool notify);

    friend class PhoneApi;
    friend class MeshtasticToRadioCallbacks;
    friend class MeshtasticFromRadioCallbacks;
    friend class MeshtasticNotifyStateCallbacks;
    friend class MeshtasticServerCallbacks;
};

} // namespace ble

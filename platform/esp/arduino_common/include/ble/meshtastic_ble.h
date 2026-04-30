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
                             public chat::ChatService::OutgoingTextObserver,
                             public team::TeamService::IncomingDataObserver,
                             public MeshtasticPhoneTransport,
                             public MeshtasticPhoneBluetoothConfigHooks,
                             public MeshtasticPhoneModuleConfigHooks,
                             public MeshtasticPhoneConfigLifecycleHooks,
                             public MeshtasticPhoneStatusHooks,
                             public MeshtasticPhoneMqttHooks,
                             public MeshtasticPhoneDeviceRuntimeHooks,
                             public IPhoneRuntimeContext
{
  public:
    MeshtasticBleService(app::IAppBleFacade& ctx, const std::string& device_name);
    ~MeshtasticBleService() override;

    bool start() override;
    void stop() override;
    void update() override;

    void onIncomingText(const chat::MeshIncomingText& msg) override;
    void onOutgoingText(const chat::MeshIncomingText& msg) override;
    void onIncomingData(const chat::MeshIncomingData& msg) override;
    bool isBleConnected() const override;
    void notifyFromNum(uint32_t value) override;
    bool loadBluetoothConfig(meshtastic_Config_BluetoothConfig* out) const override;
    void saveBluetoothConfig(const meshtastic_Config_BluetoothConfig& config) override;
    bool loadDeviceConnectionStatus(meshtastic_DeviceConnectionStatus* out) const override;
    bool loadModuleConfig(meshtastic_LocalModuleConfig* out) const override;
    void saveModuleConfig(const meshtastic_LocalModuleConfig& config) override;
    bool loadTimezoneTzdef(char* out, size_t out_len) const override;
    void saveTimezoneTzdef(const char* tzdef) override;
    int getTimezoneOffsetMinutes() const override;
    void setTimezoneOffsetMinutes(int offset_min) override;
    bool getGpsFix(MeshtasticGpsFix* out) const override;
    MeshtasticPhoneConfigSnapshot getMeshtasticPhoneConfig() const override;
    void setMeshtasticPhoneConfig(const MeshtasticPhoneConfigSnapshot& config) override;
    MeshCorePhoneConfigSnapshot getMeshCorePhoneConfig() const override;
    void setMeshCorePhoneConfig(const MeshCorePhoneConfigSnapshot& config) override;
    void saveConfig() override { ctx_.saveConfig(); }
    void applyMeshConfig() override { ctx_.applyMeshConfig(); }
    void applyUserInfo() override { ctx_.applyUserInfo(); }
    void applyPositionConfig() override { ctx_.applyPositionConfig(); }
    void getEffectiveUserInfo(char* out_long,
                              std::size_t long_len,
                              char* out_short,
                              std::size_t short_len) const override
    {
        ctx_.getEffectiveUserInfo(out_long, long_len, out_short, short_len);
    }
    chat::ChatService& getChatService() override { return ctx_.getChatService(); }
    chat::contacts::ContactService& getContactService() override { return ctx_.getContactService(); }
    chat::IMeshAdapter* getMeshAdapter() override { return ctx_.getMeshAdapter(); }
    const chat::IMeshAdapter* getMeshAdapter() const override { return ctx_.getMeshAdapter(); }
    chat::contacts::INodeStore* getNodeStore() override { return ctx_.getNodeStore(); }
    const chat::contacts::INodeStore* getNodeStore() const override { return ctx_.getNodeStore(); }
    chat::NodeId getSelfNodeId() const override { return ctx_.getSelfNodeId(); }
    bool isBleEnabled() const override { return ctx_.isBleEnabled(); }
    void setBleEnabled(bool enabled) override { ctx_.setBleEnabled(enabled); }
    bool getDeviceMacAddress(uint8_t out_mac[6]) const override { return ctx_.getDeviceMacAddress(out_mac); }
    bool syncCurrentEpochSeconds(uint32_t epoch_seconds) override
    {
        return ctx_.syncCurrentEpochSeconds(epoch_seconds);
    }
    void resetMeshConfig() override { ctx_.resetMeshConfig(); }
    void clearNodeDb() override { ctx_.clearNodeDb(); }
    void clearMessageDb() override { ctx_.clearMessageDb(); }
    void restartDevice() override { ctx_.restartDevice(); }
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

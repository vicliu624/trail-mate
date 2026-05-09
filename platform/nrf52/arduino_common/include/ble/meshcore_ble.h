#pragma once

#include "app/app_facades.h"
#include "ble_manager.h"
#include "chat/ble/meshcore_phone_core.h"
#include "chat/domain/chat_types.h"
#include "chat/ports/i_node_store.h"
#include "chat/usecase/chat_service.h"

#include <bluefruit.h>
#include <memory>
#include <string>

namespace ble
{

class MeshCoreBleService final : public BleService,
                                 public MeshCorePhoneHooks,
                                 public chat::ChatService::IncomingTextObserver,
                                 public IPhoneRuntimeContext
{
  public:
    MeshCoreBleService(app::IAppBleFacade& ctx, const std::string& device_name);
    ~MeshCoreBleService() override;

    void start() override;
    void stop() override;
    void update() override;
    bool isRunning() const override;
    void setDeviceName(const std::string& name) override;
    bool getPairingStatus(BlePairingStatus* out) const override;
    void onIncomingText(const chat::MeshIncomingText& msg) override;

    bool handleRxFrame(const uint8_t* data, size_t len);
    bool popTxFrame(uint8_t* out, size_t* out_len);

  private:
    void sendPendingNotifications();
    bool getCustomVars(std::string* out) const override;
    bool setCustomVar(const char* key, const char* value) override;
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

    app::IAppBleFacade& ctx_;
    std::string device_name_;
    ::BLEService service_;
    ::BLECharacteristic rx_char_;
    ::BLECharacteristic tx_char_;
    bool active_ = false;
    bool gatt_initialized_ = false;
    bool observer_registered_ = false;
    std::unique_ptr<MeshCorePhoneCore> core_;
};

} // namespace ble

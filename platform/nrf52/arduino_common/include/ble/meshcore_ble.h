#pragma once

#include "app/app_facades.h"
#include "ble/app_phone_facade.h"
#include "ble_manager.h"
#include "chat/domain/chat_types.h"
#include "chat/ports/i_node_store.h"
#include "chat/usecase/chat_service.h"
#include "phone/meshcore/meshcore_phone_core.h"
#include "phone/meshtastic/meshtastic_phone_core.h"

#include <bluefruit.h>
#include <memory>
#include <string>

namespace ble
{

class MeshCoreBleService final : public BleService,
                                 public phone::meshcore::MeshCorePhoneHooks,
                                 public chat::ChatService::IncomingTextObserver
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

    app::IAppBleFacade& ctx_;
    meshtastic_Config_BluetoothConfig phone_ble_config_ = meshtastic_Config_BluetoothConfig_init_zero;
    meshtastic_LocalModuleConfig phone_module_config_ = meshtastic_LocalModuleConfig_init_zero;
    AppPhoneFacade phone_facade_;
    std::string device_name_;
    ::BLEService service_;
    ::BLECharacteristic rx_char_;
    ::BLECharacteristic tx_char_;
    bool active_ = false;
    bool gatt_initialized_ = false;
    bool observer_registered_ = false;
    std::unique_ptr<phone::meshcore::MeshCorePhoneCore> core_;
};

} // namespace ble

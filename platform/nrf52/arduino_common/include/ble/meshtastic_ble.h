#pragma once

#include "app/app_facades.h"
#include "ble_manager.h"
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

#include <bluefruit.h>
#include <deque>
#include <string>

namespace ble
{

class MeshtasticBleService final : public BleService,
                                   public chat::ChatService::IncomingTextObserver
{
  public:
    struct Frame
    {
        size_t len = 0;
        uint32_t from_num = 0;
        uint8_t buf[meshtastic_FromRadio_size] = {};
    };

    MeshtasticBleService(app::IAppBleFacade& ctx, const std::string& device_name);
    ~MeshtasticBleService() override;

    void start() override;
    void stop() override;
    void update() override;
    void onIncomingText(const chat::MeshIncomingText& msg) override;
    bool handleToRadio(const uint8_t* data, size_t len);
    bool popToPhone(Frame* out);

  private:
    bool handleToRadioPacket(meshtastic_MeshPacket& packet);
    bool handleAdmin(meshtastic_MeshPacket& packet);
    bool handleLocalSelfPacket(meshtastic_MeshPacket& packet);
    void pumpIncomingAppData();
    bool encodeFromRadio(const meshtastic_FromRadio& from, uint32_t from_num, Frame* out) const;
    void enqueueQueueStatus(uint32_t packet_id, bool ok);
    void enqueueConfigSnapshot(uint32_t config_nonce);
    void enqueueFromRadio(const meshtastic_FromRadio& from, uint32_t from_num);
    void notifyFromNum(uint32_t from_num);
    meshtastic_MyNodeInfo buildMyInfo() const;
    meshtastic_NodeInfo buildSelfNodeInfo() const;
    meshtastic_NodeInfo buildNodeInfoFromEntry(const chat::contacts::NodeEntry& entry) const;
    meshtastic_DeviceMetadata buildMetadata() const;
    meshtastic_DeviceMetrics buildDeviceMetrics() const;
    meshtastic_LocalStats buildLocalStats() const;
    meshtastic_DeviceUIConfig buildDeviceUi() const;
    meshtastic_Channel buildChannel(uint8_t idx) const;
    meshtastic_Config buildConfig(meshtastic_AdminMessage_ConfigType type) const;
    meshtastic_ModuleConfig buildModuleConfig(meshtastic_AdminMessage_ModuleConfigType type) const;
    meshtastic_MeshPacket buildPacketFromText(const chat::MeshIncomingText& msg) const;
    meshtastic_MeshPacket buildPacketFromData(const chat::MeshIncomingData& msg) const;

    app::IAppBleFacade& ctx_;
    std::string device_name_;
    ::BLEService service_;
    ::BLECharacteristic to_radio_;
    ::BLECharacteristic from_radio_;
    ::BLECharacteristic from_num_;
    ::BLECharacteristic log_radio_;
    bool active_ = false;
    uint8_t last_to_radio_[meshtastic_ToRadio_size] = {};
    size_t last_to_radio_len_ = 0;
    std::deque<Frame> frame_queue_;
    std::deque<meshtastic_QueueStatus> queue_status_queue_;
    std::deque<meshtastic_MeshPacket> packet_queue_;
    meshtastic_LocalModuleConfig module_config_ = meshtastic_LocalModuleConfig_init_zero;
    char admin_canned_messages_[160] = {};
    char admin_ringtone_[96] = {};
};

} // namespace ble

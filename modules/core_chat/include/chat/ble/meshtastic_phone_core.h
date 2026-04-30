#pragma once

#include "chat/ble/phone_runtime_context.h"
#include "chat/domain/chat_types.h"
#include "chat/ports/i_node_store.h"
#include "meshtastic/admin.pb.h"
#include "meshtastic/channel.pb.h"
#include "meshtastic/config.pb.h"
#include "meshtastic/device_ui.pb.h"
#include "meshtastic/localonly.pb.h"
#include "meshtastic/mesh.pb.h"
#include "meshtastic/module_config.pb.h"
#include "meshtastic/telemetry.pb.h"

#include <deque>

namespace ble
{

struct MeshtasticBleFrame
{
    size_t len = 0;
    uint32_t from_num = 0;
    uint8_t buf[meshtastic_FromRadio_size] = {};
};

struct MeshtasticGpsFix
{
    bool valid = false;
    double lat = 0.0;
    double lng = 0.0;
    bool has_alt = false;
    double alt_m = 0.0;
    bool has_speed = false;
    double speed_mps = 0.0;
    bool has_course = false;
    double course_deg = 0.0;
    uint8_t satellites = 0;
};

class MeshtasticPhoneTransport
{
  public:
    virtual ~MeshtasticPhoneTransport() = default;
    virtual bool isBleConnected() const = 0;
    virtual void notifyFromNum(uint32_t from_num) = 0;
};

class MeshtasticPhoneBluetoothConfigHooks
{
  public:
    virtual ~MeshtasticPhoneBluetoothConfigHooks() = default;
    virtual bool loadBluetoothConfig(meshtastic_Config_BluetoothConfig* out) const
    {
        (void)out;
        return false;
    }
    virtual void saveBluetoothConfig(const meshtastic_Config_BluetoothConfig& config)
    {
        (void)config;
    }
};

class MeshtasticPhoneModuleConfigHooks
{
  public:
    virtual ~MeshtasticPhoneModuleConfigHooks() = default;
    virtual bool loadModuleConfig(meshtastic_LocalModuleConfig* out) const
    {
        (void)out;
        return false;
    }
    virtual void saveModuleConfig(const meshtastic_LocalModuleConfig& config)
    {
        (void)config;
    }
};

class MeshtasticPhoneConfigLifecycleHooks
{
  public:
    virtual ~MeshtasticPhoneConfigLifecycleHooks() = default;
    virtual void onConfigStart() {}
    virtual void onConfigComplete() {}
};

class MeshtasticPhoneStatusHooks
{
  public:
    virtual ~MeshtasticPhoneStatusHooks() = default;
    virtual bool loadDeviceConnectionStatus(meshtastic_DeviceConnectionStatus* out) const
    {
        (void)out;
        return false;
    }
};

class MeshtasticPhoneMqttHooks
{
  public:
    virtual ~MeshtasticPhoneMqttHooks() = default;
    virtual bool handleMqttProxyToRadio(const meshtastic_MqttClientProxyMessage& msg)
    {
        (void)msg;
        return false;
    }
    virtual bool pollMqttProxyToPhone(meshtastic_MqttClientProxyMessage* out)
    {
        (void)out;
        return false;
    }
};

class MeshtasticPhoneDeviceRuntimeHooks
{
  public:
    virtual ~MeshtasticPhoneDeviceRuntimeHooks() = default;
    virtual bool loadTimezoneTzdef(char* out, size_t out_len) const
    {
        (void)out;
        (void)out_len;
        return false;
    }
    virtual void saveTimezoneTzdef(const char* tzdef)
    {
        (void)tzdef;
    }
    virtual int getTimezoneOffsetMinutes() const
    {
        return 0;
    }
    virtual void setTimezoneOffsetMinutes(int offset_min)
    {
        (void)offset_min;
    }
    virtual bool getGpsFix(MeshtasticGpsFix* out) const
    {
        if (!out)
        {
            return false;
        }
        *out = {};
        return false;
    }
};

class MeshtasticPhoneCore
{
  public:
    MeshtasticPhoneCore(IPhoneRuntimeContext& ctx, MeshtasticPhoneTransport& transport,
                        MeshtasticPhoneBluetoothConfigHooks* bluetooth_config_hooks = nullptr,
                        MeshtasticPhoneModuleConfigHooks* module_config_hooks = nullptr,
                        MeshtasticPhoneConfigLifecycleHooks* config_lifecycle_hooks = nullptr,
                        MeshtasticPhoneStatusHooks* status_hooks = nullptr,
                        MeshtasticPhoneMqttHooks* mqtt_hooks = nullptr,
                        MeshtasticPhoneDeviceRuntimeHooks* device_runtime_hooks = nullptr);

    void reset();
    void pumpIncomingAppData();
    void onIncomingText(const chat::MeshIncomingText& msg);
    void onIncomingData(const chat::MeshIncomingData& msg);
    bool handleToRadio(const uint8_t* data, size_t len);
    bool popToPhone(MeshtasticBleFrame* out);
    bool isSendingPackets() const;
    bool isConfigFlowActive() const;

  private:
    bool handleToRadioPacket(meshtastic_MeshPacket& packet);
    bool handleAdmin(meshtastic_MeshPacket& packet);
    bool handleLocalSelfPacket(meshtastic_MeshPacket& packet);
    bool encodeFromRadio(const meshtastic_FromRadio& from, uint32_t from_num, MeshtasticBleFrame* out) const;
    bool popConfigSnapshotFrame(MeshtasticBleFrame* out);
    void enqueueQueueStatus(uint32_t packet_id, bool ok);
    void enqueueConfigSnapshot(uint32_t config_nonce);
    void enqueueFromRadio(const meshtastic_FromRadio& from, uint32_t from_num);
    void notifyFromNum(uint32_t from_num);
    void fillMyInfo(meshtastic_MyNodeInfo* out) const;
    void fillSelfNodeInfo(meshtastic_NodeInfo* out) const;
    void fillNodeInfoFromEntry(const chat::contacts::NodeEntry& entry, meshtastic_NodeInfo* out) const;
    void fillMetadata(meshtastic_DeviceMetadata* out) const;
    void fillDeviceUi(meshtastic_DeviceUIConfig* out) const;
    void fillChannel(uint8_t idx, meshtastic_Channel* out) const;
    void fillConfig(meshtastic_AdminMessage_ConfigType type, meshtastic_Config* out) const;
    void fillModuleConfig(meshtastic_AdminMessage_ModuleConfigType type, meshtastic_ModuleConfig* out) const;
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

    IPhoneRuntimeContext& ctx_;
    MeshtasticPhoneTransport& transport_;
    MeshtasticPhoneBluetoothConfigHooks* bluetooth_config_hooks_ = nullptr;
    MeshtasticPhoneModuleConfigHooks* module_config_hooks_ = nullptr;
    MeshtasticPhoneConfigLifecycleHooks* config_lifecycle_hooks_ = nullptr;
    MeshtasticPhoneStatusHooks* status_hooks_ = nullptr;
    MeshtasticPhoneMqttHooks* mqtt_hooks_ = nullptr;
    MeshtasticPhoneDeviceRuntimeHooks* device_runtime_hooks_ = nullptr;
    uint32_t config_nonce_ = 0;
    size_t config_node_index_ = 0;
    uint8_t config_channel_index_ = 0;
    uint8_t config_type_index_ = 0;
    uint8_t config_module_type_index_ = 0;
    uint32_t config_request_seq_ = 0;
    uint8_t last_to_radio_[meshtastic_ToRadio_size] = {};
    size_t last_to_radio_len_ = 0;
    bool config_flow_active_ = false;
    bool config_drain_empty_pending_ = false;
    std::deque<MeshtasticBleFrame> frame_queue_;
    std::deque<meshtastic_QueueStatus> queue_status_queue_;
    std::deque<meshtastic_MeshPacket> packet_queue_;
    meshtastic_Config_BluetoothConfig bluetooth_config_ = meshtastic_Config_BluetoothConfig_init_zero;
    meshtastic_LocalModuleConfig module_config_ = meshtastic_LocalModuleConfig_init_zero;
    char admin_canned_messages_[160] = {};
    char admin_ringtone_[96] = {};
    meshtastic_ToRadio to_radio_scratch_ = meshtastic_ToRadio_init_zero;
    meshtastic_AdminMessage admin_req_scratch_ = meshtastic_AdminMessage_init_zero;
    meshtastic_AdminMessage admin_resp_scratch_ = meshtastic_AdminMessage_init_zero;
    meshtastic_MeshPacket reply_packet_scratch_ = meshtastic_MeshPacket_init_zero;
    meshtastic_MqttClientProxyMessage mqtt_proxy_scratch_ = meshtastic_MqttClientProxyMessage_init_zero;
    meshtastic_FromRadio from_radio_scratch_ = meshtastic_FromRadio_init_zero;
};

} // namespace ble

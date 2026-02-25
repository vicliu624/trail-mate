#include "meshtastic_ble.h"

#include "ble_uuids.h"
#include <Arduino.h>
#include "../board/BoardBase.h"
#include "../chat/domain/contact_types.h"
#include "../chat/infra/meshtastic/mt_codec_pb.h"
#include "../chat/infra/meshtastic/mt_region.h"
#include "../chat/infra/meshtastic/generated/meshtastic/portnums.pb.h"
#include "../chat/infra/meshtastic/generated/meshtastic/telemetry.pb.h"
#include "../chat/time_utils.h"
#include <Preferences.h>
#include <algorithm>
#include <ctime>
#include <cstring>
#include <limits>

namespace ble
{

namespace
{
constexpr uint32_t kConfigNonceOnlyConfig = 69420;
constexpr uint32_t kConfigNonceOnlyNodes = 69421;

constexpr size_t kMaxFromRadio = meshtastic_FromRadio_size;
constexpr size_t kMaxToRadio = meshtastic_ToRadio_size;
constexpr uint8_t kMaxMeshtasticChannels = 8;
constexpr uint16_t kPreferredBleMtu = 517;

constexpr uint8_t kDefaultPskIndex = 1;
constexpr uint8_t kDefaultPsk[16] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                     0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};

constexpr const char* kBlePrefsNs = "mt_ble";
constexpr const char* kBleEnabledKey = "enabled";
constexpr const char* kBleModeKey = "mode";
constexpr const char* kBlePinKey = "pin";

constexpr const char* kModulePrefsNs = "mt_mod";
constexpr const char* kModuleBlobKey = "cfg";
constexpr uint32_t kModuleConfigVersion = 1;

constexpr const char* kDefaultMqttAddress = "mqtt.meshtastic.org";
constexpr const char* kDefaultMqttUsername = "meshdev";
constexpr const char* kDefaultMqttPassword = "large4cats";
constexpr const char* kDefaultMqttRoot = "msh";
constexpr bool kDefaultMqttEncryptionEnabled = true;
constexpr bool kDefaultMqttTlsEnabled = false;
constexpr uint32_t kDefaultMapPublishIntervalSecs = 60 * 60;
constexpr uint32_t kDefaultDetectionMinBroadcastSecs = 45;
constexpr uint32_t kDefaultAmbientCurrent = 10;

constexpr const char* kSettingsNs = "settings";
constexpr const char* kScreenTimeoutKey = "screen_timeout";
constexpr uint32_t kScreenTimeoutMinMs = 10000;
constexpr uint32_t kScreenTimeoutMaxMs = 300000;
constexpr uint32_t kScreenTimeoutDefaultMs = 60000;

uint8_t xorHash(const uint8_t* data, size_t len)
{
    uint8_t out = 0;
    for (size_t i = 0; i < len; ++i)
    {
        out ^= data[i];
    }
    return out;
}

uint8_t computeChannelHash(const char* name, const uint8_t* key, size_t key_len)
{
    uint8_t h = xorHash(reinterpret_cast<const uint8_t*>(name), strlen(name));
    if (key && key_len > 0)
    {
        h ^= xorHash(key, key_len);
    }
    return h;
}

uint32_t clampScreenTimeoutMs(uint32_t timeout_ms)
{
    if (timeout_ms < kScreenTimeoutMinMs)
    {
        return kScreenTimeoutDefaultMs;
    }
    if (timeout_ms > kScreenTimeoutMaxMs)
    {
        return kScreenTimeoutMaxMs;
    }
    return timeout_ms;
}

uint16_t readScreenTimeoutSecs()
{
    Preferences prefs;
    prefs.begin(kSettingsNs, true);
    uint32_t value = prefs.getUInt(kScreenTimeoutKey, 0);
    prefs.end();
    uint32_t timeout_ms = clampScreenTimeoutMs(value);
    uint32_t secs = timeout_ms / 1000;
    if (secs > 900)
    {
        secs = 900;
    }
    return static_cast<uint16_t>(secs);
}

bool isValidBlePin(uint32_t pin)
{
    return pin == 0 || (pin >= 100000 && pin <= 999999);
}

bool isZeroKey(const uint8_t* key, size_t len)
{
    if (!key || len == 0)
    {
        return true;
    }
    for (size_t i = 0; i < len; ++i)
    {
        if (key[i] != 0)
        {
            return false;
        }
    }
    return true;
}

void copyBounded(char* dst, size_t dst_len, const char* src)
{
    if (!dst || dst_len == 0)
    {
        return;
    }
    if (!src)
    {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

void fillUserId(char* dst, size_t dst_len, uint32_t node_id)
{
    if (!dst || dst_len == 0)
    {
        return;
    }
    snprintf(dst, dst_len, "!%08lX", static_cast<unsigned long>(node_id));
}

template <typename T>
void zeroInit(T& value)
{
    memset(&value, 0, sizeof(value));
}

uint32_t nowSeconds()
{
    time_t now = time(nullptr);
    if (now < 0)
    {
        return 0;
    }
    return static_cast<uint32_t>(now);
}

uint8_t channelIndexFromId(chat::ChannelId ch)
{
    return (ch == chat::ChannelId::SECONDARY) ? 1 : 0;
}

chat::ChannelId channelIdFromIndex(uint8_t idx)
{
    return (idx == 1) ? chat::ChannelId::SECONDARY : chat::ChannelId::PRIMARY;
}

meshtastic_Config_DeviceConfig_Role roleFromConfig(uint8_t role)
{
    switch (role)
    {
    case static_cast<uint8_t>(chat::contacts::NodeRoleType::Router):
        return meshtastic_Config_DeviceConfig_Role_ROUTER;
    case static_cast<uint8_t>(chat::contacts::NodeRoleType::Repeater):
        return meshtastic_Config_DeviceConfig_Role_REPEATER;
    case static_cast<uint8_t>(chat::contacts::NodeRoleType::Sensor):
        return meshtastic_Config_DeviceConfig_Role_SENSOR;
    case static_cast<uint8_t>(chat::contacts::NodeRoleType::Tracker):
        return meshtastic_Config_DeviceConfig_Role_TRACKER;
    default:
        return meshtastic_Config_DeviceConfig_Role_CLIENT;
    }
}

} // namespace

class MeshtasticServerCallbacks : public NimBLEServerCallbacks
{
  public:
    explicit MeshtasticServerCallbacks(MeshtasticBleService& owner) : owner_(owner) {}

    void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override
    {
        (void)server;
        (void)connInfo;
        owner_.connected_ = true;
        owner_.closePhoneApi();
    }

    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override
    {
        (void)server;
        (void)connInfo;
        (void)reason;
        owner_.connected_ = false;
        owner_.closePhoneApi();
        owner_.clearQueues();
    }

  private:
    MeshtasticBleService& owner_;
};

class MeshtasticToRadioCallbacks : public NimBLECharacteristicCallbacks
{
  public:
    explicit MeshtasticToRadioCallbacks(MeshtasticBleService& owner) : owner_(owner) {}

    void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override
    {
        (void)connInfo;
        if (!characteristic)
        {
            return;
        }
        NimBLEAttValue val = characteristic->getValue();
        if (val.length() == 0 || val.length() > kMaxToRadio)
        {
            return;
        }
        if (owner_.from_phone_len_ >= MeshtasticBleService::kFromPhoneQueueDepth)
        {
            return;
        }
        if (owner_.from_phone_mutex_ &&
            xSemaphoreTake(owner_.from_phone_mutex_, 0) == pdTRUE)
        {
            owner_.from_phone_queue_[owner_.from_phone_len_] = val;
            owner_.from_phone_len_++;
            xSemaphoreGive(owner_.from_phone_mutex_);
        }
    }

  private:
    MeshtasticBleService& owner_;
};

class MeshtasticFromRadioCallbacks : public NimBLECharacteristicCallbacks
{
  public:
    explicit MeshtasticFromRadioCallbacks(MeshtasticBleService& owner) : owner_(owner) {}

    void onRead(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override
    {
        (void)connInfo;
        if (!characteristic)
        {
            return;
        }
        MeshtasticBleService::Frame frame;
        bool has_frame = false;
        if (owner_.to_phone_mutex_ &&
            xSemaphoreTake(owner_.to_phone_mutex_, 0) == pdTRUE)
        {
            if (owner_.to_phone_len_ > 0)
            {
                frame = owner_.to_phone_queue_[0];
                for (size_t i = 1; i < owner_.to_phone_len_; ++i)
                {
                    owner_.to_phone_queue_[i - 1] = owner_.to_phone_queue_[i];
                }
                owner_.to_phone_len_--;
                has_frame = true;
            }
            xSemaphoreGive(owner_.to_phone_mutex_);
        }
        if (has_frame)
        {
            characteristic->setValue(frame.buf.data(), frame.len);
        }
    }

  private:
    MeshtasticBleService& owner_;
};

class MeshtasticBleService::PhoneApi
{
  public:
    explicit PhoneApi(MeshtasticBleService& service, app::AppContext& ctx)
        : service_(service),
          ctx_(ctx)
    {
        state_ = State::SendNothing;
        config_nonce_ = 0;
        config_state_ = 0;
        from_radio_num_ = 0;
        heartbeat_ = false;
        node_index_ = 0;
        file_index_ = 0;
    }

    void close()
    {
        state_ = State::SendNothing;
        config_nonce_ = 0;
        config_state_ = 0;
        from_radio_num_ = 0;
        heartbeat_ = false;
        packet_queue_.clear();
        node_index_ = 0;
        node_entries_.clear();
        file_manifest_.clear();
        file_index_ = 0;
    }

    bool handleToRadio(const uint8_t* buf, size_t len)
    {
        if (!buf || len == 0)
        {
            return false;
        }

        meshtastic_ToRadio to_radio = meshtastic_ToRadio_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(buf, len);
        if (!pb_decode(&stream, meshtastic_ToRadio_fields, &to_radio))
        {
            return false;
        }

        switch (to_radio.which_payload_variant)
        {
        case meshtastic_ToRadio_packet_tag:
            return handleToRadioPacket(to_radio.packet);
        case meshtastic_ToRadio_want_config_id_tag:
            config_nonce_ = to_radio.want_config_id;
            startConfig();
            return true;
        case meshtastic_ToRadio_disconnect_tag:
            close();
            return false;
        case meshtastic_ToRadio_heartbeat_tag:
            heartbeat_ = true;
            return true;
        default:
            break;
        }
        return false;
    }

    bool popToPhone(MeshtasticBleService::Frame& out, uint32_t& from_num)
    {
        meshtastic_FromRadio from_radio = meshtastic_FromRadio_init_zero;

        if (heartbeat_)
        {
            from_radio.which_payload_variant = meshtastic_FromRadio_queueStatus_tag;
            from_radio.queueStatus = buildQueueStatus(0);
            heartbeat_ = false;
            return encodeFromRadio(from_radio, out, from_num);
        }

        if (!available())
        {
            return false;
        }

        switch (state_)
        {
        case State::SendMyInfo:
            from_radio.which_payload_variant = meshtastic_FromRadio_my_info_tag;
            from_radio.my_info = buildMyInfo();
            state_ = State::SendDeviceUi;
            return encodeFromRadio(from_radio, out, from_num);
        case State::SendDeviceUi:
            from_radio.which_payload_variant = meshtastic_FromRadio_deviceuiConfig_tag;
            from_radio.deviceuiConfig = buildDeviceUi();
            state_ = State::SendOwnNodeInfo;
            return encodeFromRadio(from_radio, out, from_num);
        case State::SendOwnNodeInfo:
            from_radio.which_payload_variant = meshtastic_FromRadio_node_info_tag;
            from_radio.node_info = buildSelfNodeInfo();
            state_ = (config_nonce_ == kConfigNonceOnlyNodes) ? State::SendOtherNodeInfos : State::SendMetadata;
            return encodeFromRadio(from_radio, out, from_num);
        case State::SendMetadata:
            from_radio.which_payload_variant = meshtastic_FromRadio_metadata_tag;
            from_radio.metadata = buildMetadata();
            state_ = State::SendChannels;
            config_state_ = 0;
            return encodeFromRadio(from_radio, out, from_num);
        case State::SendChannels:
            from_radio.which_payload_variant = meshtastic_FromRadio_channel_tag;
            from_radio.channel = buildChannel(static_cast<uint8_t>(config_state_));
            config_state_++;
            if (config_state_ >= kMaxMeshtasticChannels)
            {
                state_ = State::SendConfig;
                config_state_ = static_cast<uint8_t>(_meshtastic_AdminMessage_ConfigType_MIN);
            }
            return encodeFromRadio(from_radio, out, from_num);
        case State::SendConfig:
            from_radio.which_payload_variant = meshtastic_FromRadio_config_tag;
            from_radio.config = buildConfig(static_cast<meshtastic_AdminMessage_ConfigType>(config_state_));
            if (config_state_ >= _meshtastic_AdminMessage_ConfigType_MAX)
            {
                state_ = State::SendModuleConfig;
                config_state_ = static_cast<uint8_t>(_meshtastic_AdminMessage_ModuleConfigType_MIN);
            }
            else
            {
                config_state_++;
            }
            return encodeFromRadio(from_radio, out, from_num);
        case State::SendModuleConfig:
            from_radio.which_payload_variant = meshtastic_FromRadio_moduleConfig_tag;
            from_radio.moduleConfig = buildModuleConfig(
                static_cast<meshtastic_AdminMessage_ModuleConfigType>(config_state_));
            if (config_state_ >= _meshtastic_AdminMessage_ModuleConfigType_MAX)
            {
                state_ = (config_nonce_ == kConfigNonceOnlyConfig) ? State::SendFileInfo : State::SendOtherNodeInfos;
                node_index_ = 0;
                cacheNodeEntries();
                config_state_ = 0;
            }
            else
            {
                config_state_++;
            }
            return encodeFromRadio(from_radio, out, from_num);
        case State::SendOtherNodeInfos:
            if (node_index_ < node_entries_.size())
            {
                from_radio.which_payload_variant = meshtastic_FromRadio_node_info_tag;
                from_radio.node_info = buildNodeInfo(node_entries_[node_index_]);
                node_index_++;
                return encodeFromRadio(from_radio, out, from_num);
            }
            state_ = State::SendFileInfo;
            return popToPhone(out, from_num);
        case State::SendFileInfo:
            if (config_nonce_ == kConfigNonceOnlyNodes || file_index_ >= file_manifest_.size())
            {
                state_ = State::SendCompleteId;
                return popToPhone(out, from_num);
            }
            from_radio.which_payload_variant = meshtastic_FromRadio_fileInfo_tag;
            from_radio.fileInfo = file_manifest_[file_index_++];
            return encodeFromRadio(from_radio, out, from_num);
        case State::SendCompleteId:
            from_radio.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
            from_radio.config_complete_id = config_nonce_;
            state_ = State::SendPackets;
            config_nonce_ = 0;
            return encodeFromRadio(from_radio, out, from_num);
        case State::SendPackets:
            if (!packet_queue_.empty())
            {
                from_radio.which_payload_variant = meshtastic_FromRadio_packet_tag;
                from_radio.packet = packet_queue_.front();
                packet_queue_.pop_front();
                return encodeFromRadio(from_radio, out, from_num);
            }
            break;
        case State::SendNothing:
        default:
            break;
        }

        return false;
    }

    void onIncomingText(const chat::MeshIncomingText& msg)
    {
        meshtastic_MeshPacket packet = buildPacketFromText(msg);
        packet_queue_.push_back(packet);
    }

    void onIncomingData(const chat::MeshIncomingData& msg)
    {
        meshtastic_MeshPacket packet = buildPacketFromData(msg);
        packet_queue_.push_back(packet);
    }

  private:
    enum class State
    {
        SendNothing = 0,
        SendMyInfo,
        SendDeviceUi,
        SendOwnNodeInfo,
        SendMetadata,
        SendChannels,
        SendConfig,
        SendModuleConfig,
        SendOtherNodeInfos,
        SendFileInfo,
        SendCompleteId,
        SendPackets
    };

    MeshtasticBleService& service_;
    app::AppContext& ctx_;
    State state_;
    uint32_t config_nonce_;
    uint8_t config_state_;
    uint32_t from_radio_num_;
    bool heartbeat_;
    std::deque<meshtastic_MeshPacket> packet_queue_;
    std::vector<chat::contacts::NodeEntry> node_entries_;
    size_t node_index_;
    std::vector<meshtastic_FileInfo> file_manifest_;
    size_t file_index_ = 0;

    void startConfig()
    {
        config_state_ = 0;
        node_index_ = 0;
        file_index_ = 0;
        cacheNodeEntries();
        buildFileManifest();
        if (config_nonce_ == kConfigNonceOnlyNodes)
        {
            state_ = State::SendOwnNodeInfo;
        }
        else
        {
            state_ = State::SendMyInfo;
        }
    }

    bool available() const
    {
        if (state_ != State::SendNothing && state_ != State::SendPackets)
        {
            return true;
        }
        if (state_ == State::SendPackets && !packet_queue_.empty())
        {
            return true;
        }
        return heartbeat_;
    }

    void cacheNodeEntries()
    {
        node_entries_.clear();
        if (auto* store = ctx_.getNodeStore())
        {
            const auto& entries = store->getEntries();
            node_entries_.assign(entries.begin(), entries.end());
        }
    }

    void buildFileManifest()
    {
        file_manifest_.clear();
        // No filesystem manifest available in this build.
    }

    meshtastic_QueueStatus buildQueueStatus(uint32_t mesh_packet_id)
    {
        meshtastic_QueueStatus status = meshtastic_QueueStatus_init_zero;
        status.res = 0;
        status.maxlen = 1;
        status.free = 1;
        status.mesh_packet_id = mesh_packet_id;
        return status;
    }

    bool encodeFromRadio(const meshtastic_FromRadio& from,
                         MeshtasticBleService::Frame& out,
                         uint32_t& from_num)
    {
        meshtastic_FromRadio msg = from;
        msg.id = ++from_radio_num_;
        pb_ostream_t ostream = pb_ostream_from_buffer(out.buf.data(), out.buf.size());
        if (!pb_encode(&ostream, meshtastic_FromRadio_fields, &msg))
        {
            return false;
        }
        out.len = static_cast<size_t>(ostream.bytes_written);
        from_num = msg.id;
        return true;
    }

    meshtastic_MyNodeInfo buildMyInfo()
    {
        meshtastic_MyNodeInfo info = meshtastic_MyNodeInfo_init_zero;
        info.my_node_num = ctx_.getSelfNodeId();
        info.reboot_count = 0;
        info.min_app_version = 0;
        size_t count = node_entries_.size() + 1;
        if (count > std::numeric_limits<uint16_t>::max())
        {
            count = std::numeric_limits<uint16_t>::max();
        }
        info.nodedb_count = static_cast<uint16_t>(count);
        uint64_t mac = ESP.getEfuseMac();
        memcpy(info.device_id.bytes, &mac, std::min(sizeof(mac), sizeof(info.device_id.bytes)));
        info.device_id.size = 8;
        strncpy(info.pio_env, "trail-mate", sizeof(info.pio_env) - 1);
        info.firmware_edition = meshtastic_FirmwareEdition_VANILLA;
        return info;
    }

    meshtastic_DeviceMetadata buildMetadata()
    {
        meshtastic_DeviceMetadata meta = meshtastic_DeviceMetadata_init_zero;
        strncpy(meta.firmware_version, "trail-mate", sizeof(meta.firmware_version) - 1);
        meta.device_state_version = 1;
        meta.canShutdown = true;
        meta.hasWifi = false;
        meta.hasBluetooth = true;
        meta.hasEthernet = false;
        meta.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
        meta.position_flags = 0;
        meta.hw_model = meshtastic_HardwareModel_T_LORA_PAGER;
        meta.hasRemoteHardware = false;
        meta.hasPKC = ctx_.getMeshAdapter() ? ctx_.getMeshAdapter()->isPkiReady() : false;
        meta.excluded_modules = 0;
        return meta;
    }

    meshtastic_DeviceUIConfig buildDeviceUi()
    {
        meshtastic_DeviceUIConfig ui = meshtastic_DeviceUIConfig_init_zero;
        ui.version = 1;
        ui.screen_brightness = 255;
        ui.screen_timeout = readScreenTimeoutSecs();
        ui.screen_lock = false;
        ui.settings_lock = false;
        ui.pin_code = 0;
        ui.theme = meshtastic_Theme_LIGHT;
        ui.alert_enabled = false;
        ui.banner_enabled = true;
        ui.ring_tone_id = 0;
        ui.language = meshtastic_Language_ENGLISH;
        ui.has_node_filter = false;
        ui.has_node_highlight = false;
        ui.has_map_data = false;
        ui.compass_mode = meshtastic_CompassMode_DYNAMIC;
        ui.screen_rgb_color = 0;
        ui.is_clockface_analog = false;
        ui.gps_format = meshtastic_DeviceUIConfig_GpsCoordinateFormat_DEC;
        return ui;
    }

    meshtastic_Channel buildChannel(uint8_t idx)
    {
        meshtastic_Channel channel = meshtastic_Channel_init_zero;
        channel.index = idx;

        const auto& cfg = ctx_.getConfig();
        bool enabled = false;
        if (idx == 0 && cfg.primary_enabled)
        {
            channel.role = meshtastic_Channel_Role_PRIMARY;
            enabled = true;
        }
        else if (idx == 1 && cfg.secondary_enabled)
        {
            channel.role = meshtastic_Channel_Role_SECONDARY;
            enabled = true;
        }
        else
        {
            channel.role = meshtastic_Channel_Role_DISABLED;
        }

        if (!enabled)
        {
            channel.has_settings = false;
            return channel;
        }

        channel.has_settings = true;
        zeroInit(channel.settings);
        channel.settings.channel_num = idx;
        channel.settings.id = 0;
        channel.settings.uplink_enabled = false;
        channel.settings.downlink_enabled = false;
        channel.settings.has_module_settings = false;

        if (idx == 0)
        {
            meshtastic_Config_LoRaConfig_ModemPreset preset =
                static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(cfg.meshtastic_config.modem_preset);
            const char* name = cfg.meshtastic_config.use_preset
                                   ? chat::meshtastic::presetDisplayName(preset)
                                   : "Custom";
            strncpy(channel.settings.name, name, sizeof(channel.settings.name) - 1);
        }
        else
        {
            strncpy(channel.settings.name, "Secondary", sizeof(channel.settings.name) - 1);
        }
        channel.settings.name[sizeof(channel.settings.name) - 1] = '\0';

        uint8_t key[16] = {};
        if (idx == 0)
        {
            memcpy(key, cfg.meshtastic_config.primary_key, sizeof(cfg.meshtastic_config.primary_key));
        }
        else
        {
            memcpy(key, cfg.meshtastic_config.secondary_key, sizeof(cfg.meshtastic_config.secondary_key));
        }

        if (!isZeroKey(key, sizeof(key)))
        {
            channel.settings.psk.size = sizeof(key);
            memcpy(channel.settings.psk.bytes, key, sizeof(key));
        }
        else if (idx == 0)
        {
            channel.settings.psk.size = 1;
            channel.settings.psk.bytes[0] = kDefaultPskIndex;
        }
        return channel;
    }

    meshtastic_Config buildConfig(meshtastic_AdminMessage_ConfigType type)
    {
        const auto& cfg = ctx_.getConfig();
        meshtastic_Config out = meshtastic_Config_init_zero;
        switch (type)
        {
        case meshtastic_AdminMessage_ConfigType_DEVICE_CONFIG:
            out.which_payload_variant = meshtastic_Config_device_tag;
            zeroInit(out.payload_variant.device);
            out.payload_variant.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
            out.payload_variant.device.rebroadcast_mode =
                cfg.chat_policy.enable_relay ? meshtastic_Config_DeviceConfig_RebroadcastMode_ALL
                                             : meshtastic_Config_DeviceConfig_RebroadcastMode_NONE;
            out.payload_variant.device.node_info_broadcast_secs = 900;
            out.payload_variant.device.serial_enabled = false;
            out.payload_variant.device.button_gpio = 0;
            out.payload_variant.device.buzzer_gpio = 0;
            out.payload_variant.device.double_tap_as_button_press = false;
            out.payload_variant.device.is_managed = false;
            out.payload_variant.device.disable_triple_click = false;
            out.payload_variant.device.tzdef[0] = '\0';
            out.payload_variant.device.led_heartbeat_disabled = false;
            out.payload_variant.device.buzzer_mode = meshtastic_Config_DeviceConfig_BuzzerMode_DISABLED;
            break;
        case meshtastic_AdminMessage_ConfigType_POSITION_CONFIG:
            out.which_payload_variant = meshtastic_Config_position_tag;
            zeroInit(out.payload_variant.position);
            out.payload_variant.position.position_broadcast_secs = 900;
            out.payload_variant.position.position_broadcast_smart_enabled = false;
            out.payload_variant.position.fixed_position = false;
            out.payload_variant.position.gps_enabled = (cfg.gps_mode != 0);
            out.payload_variant.position.gps_update_interval = cfg.gps_interval_ms / 1000;
            out.payload_variant.position.gps_attempt_time = 0;
            out.payload_variant.position.position_flags = 0;
            out.payload_variant.position.rx_gpio = 0;
            out.payload_variant.position.tx_gpio = 0;
            out.payload_variant.position.broadcast_smart_minimum_distance = 0;
            out.payload_variant.position.broadcast_smart_minimum_interval_secs = 0;
            out.payload_variant.position.gps_en_gpio = 0;
            out.payload_variant.position.gps_mode = (cfg.gps_mode != 0)
                ? meshtastic_Config_PositionConfig_GpsMode_ENABLED
                : meshtastic_Config_PositionConfig_GpsMode_DISABLED;
            break;
        case meshtastic_AdminMessage_ConfigType_POWER_CONFIG:
            out.which_payload_variant = meshtastic_Config_power_tag;
            zeroInit(out.payload_variant.power);
            out.payload_variant.power.on_battery_shutdown_after_secs = 0;
            out.payload_variant.power.is_power_saving = false;
            out.payload_variant.power.adc_multiplier_override = 0.0f;
            out.payload_variant.power.wait_bluetooth_secs = 0;
            out.payload_variant.power.sds_secs = 0;
            out.payload_variant.power.ls_secs = 0;
            out.payload_variant.power.min_wake_secs = 0;
            out.payload_variant.power.device_battery_ina_address = 0;
            out.payload_variant.power.powermon_enables = 0;
            break;
        case meshtastic_AdminMessage_ConfigType_NETWORK_CONFIG:
            out.which_payload_variant = meshtastic_Config_network_tag;
            zeroInit(out.payload_variant.network);
            out.payload_variant.network.wifi_enabled = false;
            out.payload_variant.network.wifi_ssid[0] = '\0';
            out.payload_variant.network.wifi_psk[0] = '\0';
            out.payload_variant.network.ntp_server[0] = '\0';
            out.payload_variant.network.eth_enabled = false;
            out.payload_variant.network.address_mode = meshtastic_Config_NetworkConfig_AddressMode_DHCP;
            zeroInit(out.payload_variant.network.ipv4_config);
            out.payload_variant.network.rsyslog_server[0] = '\0';
            out.payload_variant.network.enabled_protocols = 0;
            out.payload_variant.network.ipv6_enabled = false;
            break;
        case meshtastic_AdminMessage_ConfigType_DISPLAY_CONFIG:
            out.which_payload_variant = meshtastic_Config_display_tag;
            zeroInit(out.payload_variant.display);
            out.payload_variant.display.screen_on_secs = readScreenTimeoutSecs();
            out.payload_variant.display.gps_format = meshtastic_Config_DisplayConfig_DeprecatedGpsCoordinateFormat_UNUSED;
            out.payload_variant.display.auto_screen_carousel_secs = 0;
            out.payload_variant.display.compass_north_top = false;
            out.payload_variant.display.flip_screen = false;
            out.payload_variant.display.units = meshtastic_Config_DisplayConfig_DisplayUnits_METRIC;
            out.payload_variant.display.oled = meshtastic_Config_DisplayConfig_OledType_OLED_AUTO;
            out.payload_variant.display.displaymode = meshtastic_Config_DisplayConfig_DisplayMode_DEFAULT;
            out.payload_variant.display.heading_bold = false;
            out.payload_variant.display.wake_on_tap_or_motion = false;
            out.payload_variant.display.compass_orientation = meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_0;
            out.payload_variant.display.use_12h_clock = false;
            out.payload_variant.display.use_long_node_name = false;
            break;
        case meshtastic_AdminMessage_ConfigType_LORA_CONFIG:
            out.which_payload_variant = meshtastic_Config_lora_tag;
            zeroInit(out.payload_variant.lora);
            out.payload_variant.lora.use_preset = cfg.meshtastic_config.use_preset;
            out.payload_variant.lora.modem_preset =
                static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(cfg.meshtastic_config.modem_preset);
            out.payload_variant.lora.bandwidth = static_cast<uint32_t>(cfg.meshtastic_config.bandwidth_khz);
            out.payload_variant.lora.spread_factor = cfg.meshtastic_config.spread_factor;
            out.payload_variant.lora.coding_rate = cfg.meshtastic_config.coding_rate;
            out.payload_variant.lora.frequency_offset = cfg.meshtastic_config.frequency_offset_mhz;
            out.payload_variant.lora.region = static_cast<meshtastic_Config_LoRaConfig_RegionCode>(cfg.meshtastic_config.region);
            out.payload_variant.lora.hop_limit = cfg.meshtastic_config.hop_limit;
            out.payload_variant.lora.tx_enabled = cfg.meshtastic_config.tx_enabled;
            out.payload_variant.lora.tx_power = cfg.meshtastic_config.tx_power;
            out.payload_variant.lora.channel_num = cfg.meshtastic_config.channel_num;
            out.payload_variant.lora.override_duty_cycle = cfg.meshtastic_config.override_duty_cycle;
            out.payload_variant.lora.override_frequency = cfg.meshtastic_config.override_frequency_mhz;
            out.payload_variant.lora.sx126x_rx_boosted_gain = false;
            out.payload_variant.lora.pa_fan_disabled = false;
            out.payload_variant.lora.ignore_incoming_count = 0;
            out.payload_variant.lora.ignore_mqtt = false;
            out.payload_variant.lora.config_ok_to_mqtt = false;
            break;
        case meshtastic_AdminMessage_ConfigType_BLUETOOTH_CONFIG:
            out.which_payload_variant = meshtastic_Config_bluetooth_tag;
            zeroInit(out.payload_variant.bluetooth);
            out.payload_variant.bluetooth = service_.bluetoothConfig();
            break;
        case meshtastic_AdminMessage_ConfigType_SECURITY_CONFIG:
            out.which_payload_variant = meshtastic_Config_security_tag;
            zeroInit(out.payload_variant.security);
            out.payload_variant.security.is_managed = false;
            out.payload_variant.security.serial_enabled = false;
            out.payload_variant.security.debug_log_api_enabled = false;
            out.payload_variant.security.admin_channel_enabled = false;
            break;
        case meshtastic_AdminMessage_ConfigType_SESSIONKEY_CONFIG:
            out.which_payload_variant = meshtastic_Config_sessionkey_tag;
            break;
        case meshtastic_AdminMessage_ConfigType_DEVICEUI_CONFIG:
            out.which_payload_variant = meshtastic_Config_device_ui_tag;
            out.payload_variant.device_ui = buildDeviceUi();
            break;
        default:
            out.which_payload_variant = meshtastic_Config_device_tag;
            zeroInit(out.payload_variant.device);
            break;
        }
        return out;
    }

    meshtastic_ModuleConfig buildModuleConfig(meshtastic_AdminMessage_ModuleConfigType type)
    {
        meshtastic_ModuleConfig out = meshtastic_ModuleConfig_init_zero;
        const auto& module_cfg = service_.moduleConfig();
        switch (type)
        {
        case meshtastic_AdminMessage_ModuleConfigType_MQTT_CONFIG:
            out.which_payload_variant = meshtastic_ModuleConfig_mqtt_tag;
            out.payload_variant.mqtt = module_cfg.mqtt;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_SERIAL_CONFIG:
            out.which_payload_variant = meshtastic_ModuleConfig_serial_tag;
            out.payload_variant.serial = module_cfg.serial;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_EXTNOTIF_CONFIG:
            out.which_payload_variant = meshtastic_ModuleConfig_external_notification_tag;
            out.payload_variant.external_notification = module_cfg.external_notification;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_STOREFORWARD_CONFIG:
            out.which_payload_variant = meshtastic_ModuleConfig_store_forward_tag;
            out.payload_variant.store_forward = module_cfg.store_forward;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_RANGETEST_CONFIG:
            out.which_payload_variant = meshtastic_ModuleConfig_range_test_tag;
            out.payload_variant.range_test = module_cfg.range_test;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_TELEMETRY_CONFIG:
            out.which_payload_variant = meshtastic_ModuleConfig_telemetry_tag;
            out.payload_variant.telemetry = module_cfg.telemetry;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_CANNEDMSG_CONFIG:
            out.which_payload_variant = meshtastic_ModuleConfig_canned_message_tag;
            out.payload_variant.canned_message = module_cfg.canned_message;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_AUDIO_CONFIG:
            out.which_payload_variant = meshtastic_ModuleConfig_audio_tag;
            out.payload_variant.audio = module_cfg.audio;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_REMOTEHARDWARE_CONFIG:
            out.which_payload_variant = meshtastic_ModuleConfig_remote_hardware_tag;
            out.payload_variant.remote_hardware = module_cfg.remote_hardware;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_NEIGHBORINFO_CONFIG:
            out.which_payload_variant = meshtastic_ModuleConfig_neighbor_info_tag;
            out.payload_variant.neighbor_info = module_cfg.neighbor_info;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_AMBIENTLIGHTING_CONFIG:
            out.which_payload_variant = meshtastic_ModuleConfig_ambient_lighting_tag;
            out.payload_variant.ambient_lighting = module_cfg.ambient_lighting;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_DETECTIONSENSOR_CONFIG:
            out.which_payload_variant = meshtastic_ModuleConfig_detection_sensor_tag;
            out.payload_variant.detection_sensor = module_cfg.detection_sensor;
            break;
        case meshtastic_AdminMessage_ModuleConfigType_PAXCOUNTER_CONFIG:
            out.which_payload_variant = meshtastic_ModuleConfig_paxcounter_tag;
            out.payload_variant.paxcounter = module_cfg.paxcounter;
            break;
        default:
            break;
        }
        return out;
    }

    meshtastic_NodeInfo buildNodeInfo(const chat::contacts::NodeEntry& entry)
    {
        meshtastic_NodeInfo info = meshtastic_NodeInfo_init_zero;
        info.num = entry.node_id;
        info.has_user = true;
        zeroInit(info.user);
        strncpy(info.user.long_name, entry.long_name, sizeof(info.user.long_name) - 1);
        strncpy(info.user.short_name, entry.short_name, sizeof(info.user.short_name) - 1);
        fillUserId(info.user.id, sizeof(info.user.id), entry.node_id);
        info.user.hw_model = meshtastic_HardwareModel_UNSET;
        info.user.role = roleFromConfig(entry.role);
        info.snr = entry.snr;
        info.last_heard = entry.last_seen;
        info.via_mqtt = false;
        info.has_hops_away = (entry.hops_away != 0xFF);
        info.hops_away = entry.hops_away;
        info.is_favorite = false;
        info.is_ignored = false;
        info.is_key_manually_verified = false;
        return info;
    }

    meshtastic_NodeInfo buildSelfNodeInfo()
    {
        meshtastic_NodeInfo info = meshtastic_NodeInfo_init_zero;
        info.num = ctx_.getSelfNodeId();
        info.has_user = true;
        zeroInit(info.user);
        const auto& cfg = ctx_.getConfig();
        strncpy(info.user.long_name, cfg.node_name, sizeof(info.user.long_name) - 1);
        strncpy(info.user.short_name, cfg.short_name, sizeof(info.user.short_name) - 1);
        fillUserId(info.user.id, sizeof(info.user.id), info.num);
        info.user.hw_model = meshtastic_HardwareModel_T_LORA_PAGER;
        info.user.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
        info.last_heard = nowSeconds();
        info.has_hops_away = true;
        info.hops_away = 0;
        info.is_favorite = true;
        info.is_ignored = false;
        info.is_key_manually_verified = false;
        return info;
    }

    meshtastic_MeshPacket buildPacketFromText(const chat::MeshIncomingText& msg)
    {
        meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
        packet.from = msg.from;
        packet.to = msg.to;
        packet.channel = channelIndexFromId(msg.channel);
        packet.id = msg.msg_id;
        packet.rx_time = msg.timestamp;
        packet.rx_snr = msg.rx_meta.snr_db_x10 / 10.0f;
        packet.rx_rssi = msg.rx_meta.rssi_dbm_x10 / 10;
        packet.hop_limit = msg.hop_limit;
        packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        zeroInit(packet.decoded);
        packet.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        packet.decoded.dest = msg.to;
        packet.decoded.source = msg.from;
        packet.decoded.want_response = false;
        packet.decoded.has_bitfield = true;
        packet.decoded.bitfield = 0;
        size_t len = std::min(msg.text.size(), sizeof(packet.decoded.payload.bytes));
        packet.decoded.payload.size = static_cast<pb_size_t>(len);
        memcpy(packet.decoded.payload.bytes, msg.text.data(), len);
        return packet;
    }

    meshtastic_MeshPacket buildPacketFromData(const chat::MeshIncomingData& msg)
    {
        meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
        packet.from = msg.from;
        packet.to = msg.to;
        packet.channel = channelIndexFromId(msg.channel);
        packet.id = msg.packet_id;
        packet.rx_time = nowSeconds();
        packet.rx_snr = msg.rx_meta.snr_db_x10 / 10.0f;
        packet.rx_rssi = msg.rx_meta.rssi_dbm_x10 / 10;
        packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        zeroInit(packet.decoded);
        packet.decoded.portnum = static_cast<meshtastic_PortNum>(msg.portnum);
        packet.decoded.dest = msg.to;
        packet.decoded.source = msg.from;
        packet.decoded.want_response = msg.want_response;
        packet.decoded.has_bitfield = true;
        packet.decoded.bitfield = 0;
        size_t len = std::min(msg.payload.size(), sizeof(packet.decoded.payload.bytes));
        packet.decoded.payload.size = static_cast<pb_size_t>(len);
        memcpy(packet.decoded.payload.bytes, msg.payload.data(), len);
        return packet;
    }

    bool handleToRadioPacket(meshtastic_MeshPacket& packet)
    {
        if (packet.which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        {
            return false;
        }
        if (packet.decoded.portnum == meshtastic_PortNum_ADMIN_APP)
        {
            return handleAdmin(packet);
        }

        auto* adapter = ctx_.getMeshAdapter();
        if (!adapter)
        {
            return false;
        }

        chat::ChannelId ch = channelIdFromIndex(packet.channel);
        if (packet.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP)
        {
            std::string text(reinterpret_cast<char*>(packet.decoded.payload.bytes),
                             packet.decoded.payload.size);
            chat::MessageId msg_id = 0;
            return adapter->sendText(ch, text, &msg_id, packet.to);
        }

        return adapter->sendAppData(ch,
                                    static_cast<uint32_t>(packet.decoded.portnum),
                                    packet.decoded.payload.bytes,
                                    packet.decoded.payload.size,
                                    packet.to,
                                    packet.want_ack);
    }

    bool handleAdmin(meshtastic_MeshPacket& packet)
    {
        meshtastic_AdminMessage req = meshtastic_AdminMessage_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(packet.decoded.payload.bytes,
                                                     packet.decoded.payload.size);
        if (!pb_decode(&stream, meshtastic_AdminMessage_fields, &req))
        {
            return false;
        }

        meshtastic_AdminMessage resp = meshtastic_AdminMessage_init_zero;
        bool has_resp = false;

        switch (req.which_payload_variant)
        {
        case meshtastic_AdminMessage_get_owner_request_tag:
            resp.which_payload_variant = meshtastic_AdminMessage_get_owner_response_tag;
            resp.get_owner_response = buildSelfNodeInfo().user;
            has_resp = true;
            break;
        case meshtastic_AdminMessage_get_channel_request_tag:
            resp.which_payload_variant = meshtastic_AdminMessage_get_channel_response_tag;
            {
                uint32_t idx = (req.get_channel_request > 0) ? (req.get_channel_request - 1) : 0;
                resp.get_channel_response = buildChannel(static_cast<uint8_t>(idx));
            }
            has_resp = true;
            break;
        case meshtastic_AdminMessage_get_config_request_tag:
            resp.which_payload_variant = meshtastic_AdminMessage_get_config_response_tag;
            resp.get_config_response = buildConfig(req.get_config_request);
            has_resp = true;
            break;
        case meshtastic_AdminMessage_get_module_config_request_tag:
            resp.which_payload_variant = meshtastic_AdminMessage_get_module_config_response_tag;
            resp.get_module_config_response = buildModuleConfig(req.get_module_config_request);
            has_resp = true;
            break;
        case meshtastic_AdminMessage_get_device_metadata_request_tag:
            resp.which_payload_variant = meshtastic_AdminMessage_get_device_metadata_response_tag;
            resp.get_device_metadata_response = buildMetadata();
            has_resp = true;
            break;
        case meshtastic_AdminMessage_get_ui_config_request_tag:
            resp.which_payload_variant = meshtastic_AdminMessage_get_ui_config_response_tag;
            resp.get_ui_config_response = buildDeviceUi();
            has_resp = true;
            break;
        case meshtastic_AdminMessage_set_owner_tag:
        {
            auto& cfg = ctx_.getConfig();
            copyBounded(cfg.node_name, sizeof(cfg.node_name), req.set_owner.long_name);
            copyBounded(cfg.short_name, sizeof(cfg.short_name), req.set_owner.short_name);
            ctx_.saveConfig();
            ctx_.applyUserInfo();
            has_resp = false;
            break;
        }
        case meshtastic_AdminMessage_set_channel_tag:
        {
            auto& cfg = ctx_.getConfig();
            if (req.set_channel.index == 0)
            {
                cfg.primary_enabled = (req.set_channel.role != meshtastic_Channel_Role_DISABLED);
                if (req.set_channel.settings.psk.size == 16)
                {
                    memcpy(cfg.meshtastic_config.primary_key,
                           req.set_channel.settings.psk.bytes,
                           16);
                }
                else if (req.set_channel.settings.psk.size == 0)
                {
                    memset(cfg.meshtastic_config.primary_key, 0, sizeof(cfg.meshtastic_config.primary_key));
                }
                ctx_.saveConfig();
                ctx_.applyMeshConfig();
            }
            else if (req.set_channel.index == 1)
            {
                cfg.secondary_enabled = (req.set_channel.role != meshtastic_Channel_Role_DISABLED);
                if (req.set_channel.settings.psk.size == 16)
                {
                    memcpy(cfg.meshtastic_config.secondary_key,
                           req.set_channel.settings.psk.bytes,
                           16);
                }
                else if (req.set_channel.settings.psk.size == 0)
                {
                    memset(cfg.meshtastic_config.secondary_key, 0, sizeof(cfg.meshtastic_config.secondary_key));
                }
                ctx_.saveConfig();
                ctx_.applyMeshConfig();
            }
            has_resp = false;
            break;
        }
        case meshtastic_AdminMessage_set_config_tag:
        {
            auto& cfg = ctx_.getConfig();
            switch (req.set_config.which_payload_variant)
            {
            case meshtastic_Config_lora_tag:
            {
                const auto& lora = req.set_config.payload_variant.lora;
                cfg.meshtastic_config.use_preset = lora.use_preset;
                cfg.meshtastic_config.modem_preset = static_cast<uint8_t>(lora.modem_preset);
                cfg.meshtastic_config.bandwidth_khz = lora.bandwidth;
                cfg.meshtastic_config.spread_factor = static_cast<uint8_t>(lora.spread_factor);
                cfg.meshtastic_config.coding_rate = lora.coding_rate;
                cfg.meshtastic_config.frequency_offset_mhz = lora.frequency_offset;
                cfg.meshtastic_config.region = static_cast<uint8_t>(lora.region);
                cfg.meshtastic_config.hop_limit = static_cast<uint8_t>(lora.hop_limit);
                cfg.meshtastic_config.tx_enabled = lora.tx_enabled;
                cfg.meshtastic_config.tx_power = lora.tx_power;
                cfg.meshtastic_config.channel_num = lora.channel_num;
                cfg.meshtastic_config.override_duty_cycle = lora.override_duty_cycle;
                cfg.meshtastic_config.override_frequency_mhz = lora.override_frequency;
                ctx_.saveConfig();
                ctx_.applyMeshConfig();
                break;
            }
            case meshtastic_Config_position_tag:
            {
                const auto& pos = req.set_config.payload_variant.position;
                cfg.gps_mode = pos.gps_enabled ? 1 : 0;
                cfg.gps_interval_ms = pos.gps_update_interval * 1000U;
                ctx_.saveConfig();
                break;
            }
            case meshtastic_Config_display_tag:
            {
                uint32_t secs = req.set_config.payload_variant.display.screen_on_secs;
                if (secs > 0)
                {
                    uint32_t timeout_ms = clampScreenTimeoutMs(secs * 1000U);
                    Preferences prefs;
                    prefs.begin(kSettingsNs, false);
                    prefs.putUInt(kScreenTimeoutKey, timeout_ms);
                    prefs.end();
                }
                break;
            }
            case meshtastic_Config_bluetooth_tag:
            {
                service_.ble_config_ = req.set_config.payload_variant.bluetooth;
                if (!isValidBlePin(service_.ble_config_.fixed_pin))
                {
                    service_.ble_config_.fixed_pin = 0;
                }
                if (service_.ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN)
                {
                    service_.ble_config_.fixed_pin = 0;
                }
                service_.saveBleConfig();
                service_.applyBleSecurity();
                break;
            }
            case meshtastic_Config_device_ui_tag:
            {
                const auto& ui = req.set_config.payload_variant.device_ui;
                if (ui.screen_timeout > 0)
                {
                    uint32_t timeout_ms = clampScreenTimeoutMs(static_cast<uint32_t>(ui.screen_timeout) * 1000U);
                    Preferences prefs;
                    prefs.begin(kSettingsNs, false);
                    prefs.putUInt(kScreenTimeoutKey, timeout_ms);
                    prefs.end();
                }
                break;
            }
            default:
                break;
            }
            has_resp = false;
            break;
        }
        case meshtastic_AdminMessage_set_module_config_tag:
        {
            const auto& mod = req.set_module_config;
            switch (mod.which_payload_variant)
            {
            case meshtastic_ModuleConfig_mqtt_tag:
                service_.module_config_.has_mqtt = true;
                service_.module_config_.mqtt = mod.payload_variant.mqtt;
                break;
            case meshtastic_ModuleConfig_serial_tag:
                service_.module_config_.has_serial = true;
                service_.module_config_.serial = mod.payload_variant.serial;
                break;
            case meshtastic_ModuleConfig_external_notification_tag:
                service_.module_config_.has_external_notification = true;
                service_.module_config_.external_notification = mod.payload_variant.external_notification;
                break;
            case meshtastic_ModuleConfig_store_forward_tag:
                service_.module_config_.has_store_forward = true;
                service_.module_config_.store_forward = mod.payload_variant.store_forward;
                break;
            case meshtastic_ModuleConfig_range_test_tag:
                service_.module_config_.has_range_test = true;
                service_.module_config_.range_test = mod.payload_variant.range_test;
                break;
            case meshtastic_ModuleConfig_telemetry_tag:
                service_.module_config_.has_telemetry = true;
                service_.module_config_.telemetry = mod.payload_variant.telemetry;
                break;
            case meshtastic_ModuleConfig_canned_message_tag:
                service_.module_config_.has_canned_message = true;
                service_.module_config_.canned_message = mod.payload_variant.canned_message;
                break;
            case meshtastic_ModuleConfig_audio_tag:
                service_.module_config_.has_audio = true;
                service_.module_config_.audio = mod.payload_variant.audio;
                break;
            case meshtastic_ModuleConfig_remote_hardware_tag:
                service_.module_config_.has_remote_hardware = true;
                service_.module_config_.remote_hardware = mod.payload_variant.remote_hardware;
                break;
            case meshtastic_ModuleConfig_neighbor_info_tag:
                service_.module_config_.has_neighbor_info = true;
                service_.module_config_.neighbor_info = mod.payload_variant.neighbor_info;
                break;
            case meshtastic_ModuleConfig_ambient_lighting_tag:
                service_.module_config_.has_ambient_lighting = true;
                service_.module_config_.ambient_lighting = mod.payload_variant.ambient_lighting;
                break;
            case meshtastic_ModuleConfig_detection_sensor_tag:
                service_.module_config_.has_detection_sensor = true;
                service_.module_config_.detection_sensor = mod.payload_variant.detection_sensor;
                break;
            case meshtastic_ModuleConfig_paxcounter_tag:
                service_.module_config_.has_paxcounter = true;
                service_.module_config_.paxcounter = mod.payload_variant.paxcounter;
                break;
            default:
                break;
            }
            service_.module_config_.version = kModuleConfigVersion;
            service_.saveModuleConfig();
            has_resp = false;
            break;
        }
        case meshtastic_AdminMessage_store_ui_config_tag:
        {
            const auto& ui = req.store_ui_config;
            if (ui.screen_timeout > 0)
            {
                uint32_t timeout_ms = clampScreenTimeoutMs(static_cast<uint32_t>(ui.screen_timeout) * 1000U);
                Preferences prefs;
                prefs.begin(kSettingsNs, false);
                prefs.putUInt(kScreenTimeoutKey, timeout_ms);
                prefs.end();
            }
            has_resp = false;
            break;
        }
        default:
            break;
        }

        if (has_resp)
        {
            return sendAdminResponse(resp);
        }
        return true;
    }

    bool sendAdminResponse(const meshtastic_AdminMessage& resp)
    {
        uint8_t payload[256];
        pb_ostream_t ostream = pb_ostream_from_buffer(payload, sizeof(payload));
        if (!pb_encode(&ostream, meshtastic_AdminMessage_fields, &resp))
        {
            return false;
        }

        meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
        packet.from = ctx_.getSelfNodeId();
        packet.to = 0;
        packet.channel = 0;
        packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        zeroInit(packet.decoded);
        packet.decoded.portnum = meshtastic_PortNum_ADMIN_APP;
        packet.decoded.payload.size = static_cast<pb_size_t>(ostream.bytes_written);
        memcpy(packet.decoded.payload.bytes, payload, ostream.bytes_written);
        packet_queue_.push_back(packet);
        return true;
    }
};

MeshtasticBleService::MeshtasticBleService(app::AppContext& ctx, const std::string& device_name)
    : ctx_(ctx),
      device_name_(device_name)
{
}

MeshtasticBleService::~MeshtasticBleService()
{
    stop();
}

void MeshtasticBleService::loadBleConfig()
{
    ble_config_ = meshtastic_Config_BluetoothConfig_init_zero;
    ble_config_.enabled = true;
    ble_config_.mode = meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN;
    ble_config_.fixed_pin = 0;

    Preferences prefs;
    if (prefs.begin(kBlePrefsNs, true))
    {
        ble_config_.enabled = prefs.getBool(kBleEnabledKey, true);
        uint8_t mode = prefs.getUChar(kBleModeKey,
                                      static_cast<uint8_t>(meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN));
        if (mode <= static_cast<uint8_t>(meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN))
        {
            ble_config_.mode = static_cast<meshtastic_Config_BluetoothConfig_PairingMode>(mode);
        }
        uint32_t pin = prefs.getUInt(kBlePinKey, 0);
        if (isValidBlePin(pin))
        {
            ble_config_.fixed_pin = pin;
        }
        prefs.end();
    }
    if (ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN)
    {
        ble_config_.fixed_pin = 0;
    }
}

void MeshtasticBleService::saveBleConfig()
{
    Preferences prefs;
    if (prefs.begin(kBlePrefsNs, false))
    {
        prefs.putBool(kBleEnabledKey, ble_config_.enabled);
        prefs.putUChar(kBleModeKey, static_cast<uint8_t>(ble_config_.mode));
        prefs.putUInt(kBlePinKey, ble_config_.fixed_pin);
        prefs.end();
    }
}

void MeshtasticBleService::applyBleSecurity()
{
    if (!ble_config_.enabled)
    {
        return;
    }

    if (ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN)
    {
        return;
    }

    // Bonding data is stored in NVS by NimBLE. If you see "NIMBLE_NVS: NVS write operation failed",
    // NVS may be full or corrupted: run "pio run -t erase" then reflash firmware (config will reset).
    NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND | BLE_SM_PAIR_AUTHREQ_MITM | BLE_SM_PAIR_AUTHREQ_SC);
    NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);

    if (ble_config_.fixed_pin != 0)
    {
        NimBLEDevice::setSecurityPasskey(ble_config_.fixed_pin);
    }
}

void MeshtasticBleService::loadModuleConfig()
{
    auto init_defaults = [&]()
    {
        zeroInit(module_config_);
        module_config_.version = kModuleConfigVersion;
        module_config_.has_mqtt = true;
        module_config_.has_serial = true;
        module_config_.has_external_notification = true;
        module_config_.has_store_forward = true;
        module_config_.has_range_test = true;
        module_config_.has_telemetry = true;
        module_config_.has_canned_message = true;
        module_config_.has_audio = true;
        module_config_.has_remote_hardware = true;
        module_config_.has_neighbor_info = true;
        module_config_.has_ambient_lighting = true;
        module_config_.has_detection_sensor = true;
        module_config_.has_paxcounter = true;

        copyBounded(module_config_.mqtt.address, sizeof(module_config_.mqtt.address), kDefaultMqttAddress);
        copyBounded(module_config_.mqtt.username, sizeof(module_config_.mqtt.username), kDefaultMqttUsername);
        copyBounded(module_config_.mqtt.password, sizeof(module_config_.mqtt.password), kDefaultMqttPassword);
        copyBounded(module_config_.mqtt.root, sizeof(module_config_.mqtt.root), kDefaultMqttRoot);
        module_config_.mqtt.encryption_enabled = kDefaultMqttEncryptionEnabled;
        module_config_.mqtt.tls_enabled = kDefaultMqttTlsEnabled;
        module_config_.mqtt.has_map_report_settings = true;
        module_config_.mqtt.map_report_settings.publish_interval_secs = kDefaultMapPublishIntervalSecs;
        module_config_.mqtt.map_report_settings.position_precision = 0;
        module_config_.mqtt.map_report_settings.should_report_location = false;

        module_config_.neighbor_info.enabled = false;
        module_config_.neighbor_info.update_interval = 0;
        module_config_.neighbor_info.transmit_over_lora = false;

        module_config_.detection_sensor.enabled = false;
        module_config_.detection_sensor.detection_trigger_type =
            meshtastic_ModuleConfig_DetectionSensorConfig_TriggerType_LOGIC_HIGH;
        module_config_.detection_sensor.minimum_broadcast_secs = kDefaultDetectionMinBroadcastSecs;

        module_config_.ambient_lighting.current = kDefaultAmbientCurrent;
        uint32_t node_id = ctx_.getSelfNodeId();
        module_config_.ambient_lighting.red = (node_id >> 16) & 0xFF;
        module_config_.ambient_lighting.green = (node_id >> 8) & 0xFF;
        module_config_.ambient_lighting.blue = node_id & 0xFF;

#if defined(ROTARY_A) && defined(ROTARY_B) && defined(ROTARY_C)
        module_config_.canned_message.updown1_enabled = true;
        module_config_.canned_message.inputbroker_pin_a = ROTARY_A;
        module_config_.canned_message.inputbroker_pin_b = ROTARY_B;
        module_config_.canned_message.inputbroker_pin_press = ROTARY_C;
        module_config_.canned_message.inputbroker_event_cw =
            meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar(28);
        module_config_.canned_message.inputbroker_event_ccw =
            meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar(29);
        module_config_.canned_message.inputbroker_event_press =
            meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_SELECT;
#endif
    };

    init_defaults();

    Preferences prefs;
    if (prefs.begin(kModulePrefsNs, true))
    {
        size_t len = prefs.getBytes(kModuleBlobKey, &module_config_, sizeof(module_config_));
        prefs.end();
        if (len != sizeof(module_config_) || module_config_.version != kModuleConfigVersion)
        {
            init_defaults();
        }
    }
}

void MeshtasticBleService::saveModuleConfig()
{
    Preferences prefs;
    if (prefs.begin(kModulePrefsNs, false))
    {
        prefs.putBytes(kModuleBlobKey, &module_config_, sizeof(module_config_));
        prefs.end();
    }
}

void MeshtasticBleService::start()
{
    loadBleConfig();
    loadModuleConfig();
    NimBLEDevice::setMTU(kPreferredBleMtu);
    applyBleSecurity();

    from_phone_mutex_ = xSemaphoreCreateMutex();
    to_phone_mutex_ = xSemaphoreCreateMutex();

    server_ = NimBLEDevice::createServer();
    server_->setCallbacks(new MeshtasticServerCallbacks(*this));

    setupService();
    startAdvertising();

    ctx_.getChatService().addIncomingTextObserver(this);
    if (auto* team = ctx_.getTeamService())
    {
        team->addIncomingDataObserver(this);
    }

    phone_api_.reset(new PhoneApi(*this, ctx_));
}

void MeshtasticBleService::stop()
{
    ctx_.getChatService().removeIncomingTextObserver(this);
    if (auto* team = ctx_.getTeamService())
    {
        team->removeIncomingDataObserver(this);
    }

    if (server_)
    {
        server_->getAdvertising()->stop();
    }

    if (from_phone_mutex_)
    {
        vSemaphoreDelete(from_phone_mutex_);
        from_phone_mutex_ = nullptr;
    }
    if (to_phone_mutex_)
    {
        vSemaphoreDelete(to_phone_mutex_);
        to_phone_mutex_ = nullptr;
    }

    clearQueues();
    service_ = nullptr;
    server_ = nullptr;
    to_radio_ = nullptr;
    from_radio_ = nullptr;
    from_num_ = nullptr;
    log_radio_ = nullptr;
    phone_api_.reset();
}

void MeshtasticBleService::update()
{
    handleFromPhone();
    handleToPhone();
}

void MeshtasticBleService::onIncomingText(const chat::MeshIncomingText& msg)
{
    if (phone_api_)
    {
        phone_api_->onIncomingText(msg);
    }
}

void MeshtasticBleService::onIncomingData(const chat::MeshIncomingData& msg)
{
    if (phone_api_)
    {
        phone_api_->onIncomingData(msg);
    }
}

void MeshtasticBleService::setupService()
{
    service_ = server_->createService(MESH_SERVICE_UUID);

    to_radio_ = service_->createCharacteristic(TORADIO_UUID, NIMBLE_PROPERTY::WRITE);
    from_radio_ = service_->createCharacteristic(FROMRADIO_UUID, NIMBLE_PROPERTY::READ);
    from_num_ = service_->createCharacteristic(FROMNUM_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    log_radio_ = service_->createCharacteristic(LOGRADIO_UUID, NIMBLE_PROPERTY::WRITE);

    to_radio_->setCallbacks(new MeshtasticToRadioCallbacks(*this));
    from_radio_->setCallbacks(new MeshtasticFromRadioCallbacks(*this));

    service_->start();
}

void MeshtasticBleService::startAdvertising()
{
    if (!server_)
    {
        return;
    }
    NimBLEAdvertising* adv = server_->getAdvertising();
    adv->addServiceUUID(MESH_SERVICE_UUID);
    adv->enableScanResponse(true);
    adv->start();
}

void MeshtasticBleService::handleFromPhone()
{
    if (!phone_api_)
    {
        return;
    }
    if (!from_phone_mutex_)
    {
        return;
    }

    while (from_phone_len_ > 0)
    {
        NimBLEAttValue val;
        if (xSemaphoreTake(from_phone_mutex_, 0) == pdTRUE)
        {
            val = from_phone_queue_[0];
            for (size_t i = 1; i < from_phone_len_; ++i)
            {
                from_phone_queue_[i - 1] = from_phone_queue_[i];
            }
            from_phone_len_--;
            xSemaphoreGive(from_phone_mutex_);
        }
        else
        {
            break;
        }
        phone_api_->handleToRadio(reinterpret_cast<const uint8_t*>(val.data()), val.length());
    }
}

void MeshtasticBleService::handleToPhone()
{
    if (!phone_api_ || !to_phone_mutex_)
    {
        return;
    }

    while (to_phone_len_ < kToPhoneQueueDepth)
    {
        MeshtasticBleService::Frame frame;
        uint32_t from_num = 0;
        if (!phone_api_->popToPhone(frame, from_num))
        {
            break;
        }
        if (xSemaphoreTake(to_phone_mutex_, 0) == pdTRUE)
        {
            to_phone_queue_[to_phone_len_++] = frame;
            xSemaphoreGive(to_phone_mutex_);
            notifyFromNum(from_num);
        }
        else
        {
            break;
        }
    }
}

void MeshtasticBleService::notifyFromNum(uint32_t value)
{
    if (!from_num_)
    {
        return;
    }
    from_num_->setValue(value);
    if (connected_)
    {
        from_num_->notify();
    }
}

void MeshtasticBleService::clearQueues()
{
    if (from_phone_mutex_)
    {
        if (xSemaphoreTake(from_phone_mutex_, 0) == pdTRUE)
        {
            from_phone_len_ = 0;
            xSemaphoreGive(from_phone_mutex_);
        }
    }
    if (to_phone_mutex_)
    {
        if (xSemaphoreTake(to_phone_mutex_, 0) == pdTRUE)
        {
            to_phone_len_ = 0;
            xSemaphoreGive(to_phone_mutex_);
        }
    }
}

void MeshtasticBleService::closePhoneApi()
{
    if (phone_api_)
    {
        phone_api_->close();
    }
}

} // namespace ble

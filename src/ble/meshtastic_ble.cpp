#include "meshtastic_ble.h"

#include "../board/BoardBase.h"
#include "../chat/domain/contact_types.h"
#include "../chat/infra/mesh_adapter_router.h"
#include "../chat/infra/meshtastic/generated/meshtastic/portnums.pb.h"
#include "../chat/infra/meshtastic/generated/meshtastic/telemetry.pb.h"
#include "../chat/infra/meshtastic/mt_adapter.h"
#include "../chat/infra/meshtastic/mt_codec_pb.h"
#include "../chat/infra/meshtastic/mt_region.h"
#include "../chat/time_utils.h"
#include "../gps/gps_service_api.h"
#include "../screen_sleep.h"
#include "../ui/widgets/ble_pairing_popup.h"
#include "ble_uuids.h"
#include <Arduino.h>
#include <NimBLE2904.h>
#include <Preferences.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <limits>
#include <sys/time.h>

namespace ble
{

namespace
{
chat::meshtastic::MtAdapter* getMeshtasticBackend(app::AppContext& ctx);
std::string channelDisplayName(app::AppContext& ctx, uint8_t idx);

// Lightweight BLE/Meshtastic debug logging. Always uses Serial so you can
// see exchange_user_info / exchange_positions behavior in the monitor.
inline void ble_log(const char* fmt, ...)
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.printf("[BLE] %s\n", buf);
}
constexpr uint32_t kConfigNonceOnlyConfig = 69420;
constexpr uint32_t kConfigNonceOnlyNodes = 69421;

constexpr size_t kMaxFromRadio = meshtastic_FromRadio_size;
constexpr size_t kMaxToRadio = meshtastic_ToRadio_size;
constexpr uint8_t kMaxMeshtasticChannels = 8;
constexpr uint16_t kPreferredBleMtu = 517;
constexpr bool kEnableFromRadioSync = false;
constexpr uint32_t kOfficialMinAppVersion = 30200;
constexpr uint32_t kOfficialDeviceStateVersion = 24;
constexpr const char* kCompatFirmwareVersion = "2.7.4.0";

const char* pairingModeName(meshtastic_Config_BluetoothConfig_PairingMode mode)
{
    switch (mode)
    {
    case meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN:
        return "random_pin";
    case meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN:
        return "fixed_pin";
    case meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN:
        return "no_pin";
    default:
        return "unknown";
    }
}

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

    uint32_t onPassKeyDisplay() override
    {
        uint32_t passkey = owner_.ble_config_.fixed_pin;
        if (owner_.ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN ||
            !isValidBlePin(passkey))
        {
            passkey = static_cast<uint32_t>(random(100000, 1000000));
        }
        owner_.pending_passkey_.store(passkey);
        ble_log("pairing passkey=%06lu", static_cast<unsigned long>(passkey));
        return passkey;
    }

    void onAuthenticationComplete(NimBLEConnInfo& connInfo) override
    {
        ble_log("auth complete bonded=%u enc=%u auth=%u handle=%u",
                connInfo.isBonded() ? 1U : 0U,
                connInfo.isEncrypted() ? 1U : 0U,
                connInfo.isAuthenticated() ? 1U : 0U,
                static_cast<unsigned>(connInfo.getConnHandle()));
        owner_.pending_passkey_.store(0);
    }

    void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override
    {
        (void)server;
        owner_.connected_ = true;
        owner_.conn_handle_ = connInfo.getConnHandle();
        owner_.conn_handle_valid_ = true;
        owner_.read_waiting_.store(false);
        owner_.from_num_subscribed_ = false;
        owner_.from_radio_sync_subscribed_ = false;
        owner_.from_radio_sync_sub_value_ = 0;
        owner_.last_to_radio_len_ = 0;
        memset(owner_.last_to_radio_.data(), 0, owner_.last_to_radio_.size());
        owner_.closePhoneApi();
        if (owner_.server_)
        {
            owner_.server_->updateConnParams(owner_.conn_handle_, 6, 12, 0, 200);
        }
        owner_.refreshBatteryLevel(false);
        ble_log("connected; uuid=%s addr=%s mtu=%u bonded=%u",
                MESH_SERVICE_UUID,
                connInfo.getAddress().toString().c_str(),
                static_cast<unsigned>(connInfo.getMTU()),
                connInfo.isBonded() ? 1U : 0U);
    }

    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override
    {
        (void)server;
        (void)connInfo;
        owner_.connected_ = false;
        owner_.conn_handle_valid_ = false;
        owner_.conn_handle_ = 0;
        owner_.read_waiting_.store(false);
        owner_.from_num_subscribed_ = false;
        owner_.from_radio_sync_subscribed_ = false;
        owner_.from_radio_sync_sub_value_ = 0;
        owner_.pending_passkey_.store(0);
        owner_.last_to_radio_len_ = 0;
        memset(owner_.last_to_radio_.data(), 0, owner_.last_to_radio_.size());
        owner_.closePhoneApi();
        owner_.clearQueues();
        owner_.startAdvertising();
        ble_log("disconnected reason=%d; advertising restarted uuid=%s", reason, MESH_SERVICE_UUID);
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
        if (owner_.last_to_radio_len_ == val.length() &&
            memcmp(owner_.last_to_radio_.data(), val.data(), val.length()) == 0)
        {
            ble_log("fromPhone drop duplicate len=%u", static_cast<unsigned>(val.length()));
            return;
        }
        if (owner_.from_phone_mutex_ &&
            xSemaphoreTake(owner_.from_phone_mutex_, portMAX_DELAY) == pdTRUE)
        {
            if (owner_.from_phone_len_ < MeshtasticBleService::kFromPhoneQueueDepth)
            {
                owner_.from_phone_queue_[owner_.from_phone_len_] = val;
                owner_.from_phone_len_++;
                owner_.last_to_radio_len_ = val.length();
                memcpy(owner_.last_to_radio_.data(), val.data(), val.length());
            }
            else
            {
                ble_log("fromPhone drop len=%u (queue full)", static_cast<unsigned>(val.length()));
            }
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

        auto tryPopFrame = [&]() -> bool
        {
            if (owner_.to_phone_mutex_ &&
                xSemaphoreTake(owner_.to_phone_mutex_, pdMS_TO_TICKS(20)) == pdTRUE)
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
            return has_frame;
        };

        if (!tryPopFrame() && owner_.shouldBlockOnRead())
        {
            owner_.read_waiting_.store(true);
            int tries = 0;
            while (!has_frame && owner_.read_waiting_.load() && tries < 4000)
            {
                delay((tries < 20) ? 1 : 5);
                tryPopFrame();
                tries++;
            }
            owner_.read_waiting_.store(false);
            if (!has_frame)
            {
                ble_log("fromRadio wait timeout; return empty");
            }
        }

        if (has_frame)
        {
            characteristic->setValue(frame.buf.data(), frame.len);
            ble_log("fromRadio read len=%u", static_cast<unsigned>(frame.len));
        }
        else
        {
            // Important: clear characteristic value when queue is empty.
            // The mobile app drains by repeated READ until it gets zero-length data.
            characteristic->setValue(reinterpret_cast<const uint8_t*>(""), 0);
            ble_log("fromRadio read empty");
        }
    }

  private:
    MeshtasticBleService& owner_;
};

class MeshtasticNotifyStateCallbacks : public NimBLECharacteristicCallbacks
{
  public:
    explicit MeshtasticNotifyStateCallbacks(MeshtasticBleService& owner) : owner_(owner) {}

    void onSubscribe(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override
    {
        (void)connInfo;
        if (characteristic == owner_.from_num_)
        {
            owner_.from_num_subscribed_ = (subValue != 0);
            ble_log("fromNum subscribe sub=0x%X", static_cast<unsigned>(subValue));
            return;
        }
        if (characteristic == owner_.from_radio_sync_)
        {
            owner_.from_radio_sync_subscribed_ = (subValue != 0);
            owner_.from_radio_sync_sub_value_ = subValue;
            ble_log("fromRadioSync subscribe sub=0x%X", static_cast<unsigned>(subValue));
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
        fallback_packet_id_ = 1;
        admin_ringtone_[0] = '\0';
        admin_canned_messages_[0] = '\0';
        recent_to_radio_ids_.fill(0);
    }

    void close()
    {
        state_ = State::SendNothing;
        config_nonce_ = 0;
        config_state_ = 0;
        from_radio_num_ = 0;
        heartbeat_ = false;
        packet_queue_.clear();
        queue_status_queue_.clear();
        mqtt_proxy_queue_.clear();
        node_index_ = 0;
        node_entries_.clear();
        file_manifest_.clear();
        file_index_ = 0;
        fallback_packet_id_ = 1;
        recent_to_radio_ids_.fill(0);
    }

    bool isSendingPackets() const
    {
        return state_ == State::SendPackets;
    }

    bool isConfigFlowActive() const
    {
        return state_ != State::SendNothing && state_ != State::SendPackets;
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
            ble_log("ToRadio packet");
            return handleToRadioPacket(to_radio.packet);
        case meshtastic_ToRadio_mqttClientProxyMessage_tag:
        {
            ble_log("ToRadio mqtt proxy topic='%s' bytes=%u",
                    to_radio.mqttClientProxyMessage.topic,
                    static_cast<unsigned>(to_radio.mqttClientProxyMessage.which_payload_variant ==
                                                  meshtastic_MqttClientProxyMessage_data_tag
                                              ? to_radio.mqttClientProxyMessage.payload_variant.data.size
                                              : strlen(to_radio.mqttClientProxyMessage.payload_variant.text)));
            auto* mt = getMeshtasticBackend(ctx_);
            return mt ? mt->handleMqttProxyMessage(to_radio.mqttClientProxyMessage) : false;
        }
        case meshtastic_ToRadio_want_config_id_tag:
            config_nonce_ = to_radio.want_config_id;
            ble_log("ToRadio want_config_id=%lu", static_cast<unsigned long>(config_nonce_));
            startConfig();
            return true;
        case meshtastic_ToRadio_disconnect_tag:
            ble_log("ToRadio disconnect");
            close();
            return false;
        case meshtastic_ToRadio_heartbeat_tag:
            ble_log("ToRadio heartbeat");
            heartbeat_ = true;
            return true;
        default:
            ble_log("ToRadio unknown tag=%d", static_cast<int>(to_radio.which_payload_variant));
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
            ble_log("tx my_info node=%08lX nodedb=%u",
                    static_cast<unsigned long>(from_radio.my_info.my_node_num),
                    static_cast<unsigned>(from_radio.my_info.nodedb_count));
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
            ble_log("tx config_complete_id=%lu", static_cast<unsigned long>(config_nonce_));
            state_ = State::SendPackets;
            config_nonce_ = 0;
            service_.requestLowerPowerConnection();
            if (!queue_status_queue_.empty() || !packet_queue_.empty())
            {
                // Wake the app to drain pending admin/queue-status packets now that we are in steady state.
                service_.notifyFromNum(0);
            }
            return encodeFromRadio(from_radio, out, from_num);
        case State::SendPackets:
        case State::SendNothing:
            if (!queue_status_queue_.empty())
            {
                from_radio.which_payload_variant = meshtastic_FromRadio_queueStatus_tag;
                from_radio.queueStatus = queue_status_queue_.front();
                queue_status_queue_.pop_front();
                ble_log("pop queue_status id=%lu res=%u",
                        static_cast<unsigned long>(from_radio.queueStatus.mesh_packet_id),
                        static_cast<unsigned>(from_radio.queueStatus.res));
                return encodeFromRadio(from_radio, out, from_num);
            }
            if (!mqtt_proxy_queue_.empty())
            {
                from_radio.which_payload_variant = meshtastic_FromRadio_mqttClientProxyMessage_tag;
                from_radio.mqttClientProxyMessage = mqtt_proxy_queue_.front();
                mqtt_proxy_queue_.pop_front();
                ble_log("pop mqtt proxy topic='%s' bytes=%u",
                        from_radio.mqttClientProxyMessage.topic,
                        static_cast<unsigned>(from_radio.mqttClientProxyMessage.which_payload_variant ==
                                                      meshtastic_MqttClientProxyMessage_data_tag
                                                  ? from_radio.mqttClientProxyMessage.payload_variant.data.size
                                                  : strlen(from_radio.mqttClientProxyMessage.payload_variant.text)));
                return encodeFromRadio(from_radio, out, from_num);
            }
            if (!packet_queue_.empty())
            {
                from_radio.which_payload_variant = meshtastic_FromRadio_packet_tag;
                from_radio.packet = packet_queue_.front();
                packet_queue_.pop_front();
                if (from_radio.packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag)
                {
                    ble_log("pop packet id=%lu port=%u from=%08lX to=%08lX payload=%u",
                            static_cast<unsigned long>(from_radio.packet.id),
                            static_cast<unsigned>(from_radio.packet.decoded.portnum),
                            static_cast<unsigned long>(from_radio.packet.from),
                            static_cast<unsigned long>(from_radio.packet.to),
                            static_cast<unsigned>(from_radio.packet.decoded.payload.size));
                }
                return encodeFromRadio(from_radio, out, from_num);
            }
            break;
        default:
            break;
        }

        return false;
    }

    void onIncomingText(const chat::MeshIncomingText& msg)
    {
        meshtastic_MeshPacket packet = buildPacketFromText(msg);
        packet_queue_.push_back(packet);
        ble_log("queue text id=%lu from=%08lX to=%08lX q=%u",
                static_cast<unsigned long>(packet.id),
                static_cast<unsigned long>(packet.from),
                static_cast<unsigned long>(packet.to),
                static_cast<unsigned>(packet_queue_.size()));
    }

    void onIncomingData(const chat::MeshIncomingData& msg)
    {
        meshtastic_MeshPacket packet = buildPacketFromData(msg);
        packet_queue_.push_back(packet);
        ble_log("queue data id=%lu port=%u from=%08lX to=%08lX q=%u",
                static_cast<unsigned long>(packet.id),
                static_cast<unsigned>(packet.decoded.portnum),
                static_cast<unsigned long>(packet.from),
                static_cast<unsigned long>(packet.to),
                static_cast<unsigned>(packet_queue_.size()));
    }

    void onMqttProxyMessage(const meshtastic_MqttClientProxyMessage& msg)
    {
        mqtt_proxy_queue_.push_back(msg);
        ble_log("queue mqtt proxy topic='%s' q=%u",
                msg.topic,
                static_cast<unsigned>(mqtt_proxy_queue_.size()));
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
    uint32_t fallback_packet_id_;
    char admin_ringtone_[231];
    char admin_canned_messages_[201];
    bool heartbeat_;
    std::deque<meshtastic_MeshPacket> packet_queue_;
    std::deque<meshtastic_QueueStatus> queue_status_queue_;
    std::deque<meshtastic_MqttClientProxyMessage> mqtt_proxy_queue_;
    std::vector<chat::contacts::NodeEntry> node_entries_;
    size_t node_index_;
    std::vector<meshtastic_FileInfo> file_manifest_;
    size_t file_index_ = 0;
    std::array<uint32_t, 20> recent_to_radio_ids_{};

    bool wasSeenRecently(uint32_t id)
    {
        if (id == 0)
        {
            return false;
        }
        for (size_t i = 0; i < recent_to_radio_ids_.size(); ++i)
        {
            if (recent_to_radio_ids_[i] == id)
            {
                return true;
            }
            if (recent_to_radio_ids_[i] == 0)
            {
                recent_to_radio_ids_[i] = id;
                return false;
            }
        }
        for (size_t i = 1; i < recent_to_radio_ids_.size(); ++i)
        {
            recent_to_radio_ids_[i - 1] = recent_to_radio_ids_[i];
        }
        recent_to_radio_ids_.back() = id;
        return false;
    }

    void startConfig()
    {
        config_state_ = 0;
        node_index_ = 0;
        file_index_ = 0;
        cacheNodeEntries();
        buildFileManifest();
        service_.requestHighThroughputConnection();
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
        if (!queue_status_queue_.empty())
        {
            return true;
        }
        if (!mqtt_proxy_queue_.empty())
        {
            return true;
        }
        if (!packet_queue_.empty())
        {
            return true;
        }
        if (state_ != State::SendNothing && state_ != State::SendPackets)
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
        // Match Meshtastic default queue status scale used by the phone app.
        status.maxlen = 16;
        status.free = 16;
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

    uint32_t normalizePacketId(uint32_t candidate)
    {
        if (candidate != 0)
        {
            if (candidate >= fallback_packet_id_)
            {
                fallback_packet_id_ = candidate + 1;
                if (fallback_packet_id_ == 0)
                {
                    fallback_packet_id_ = 1;
                }
            }
            return candidate;
        }

        if (fallback_packet_id_ == 0)
        {
            fallback_packet_id_ = 1;
        }
        return fallback_packet_id_++;
    }

    meshtastic_MyNodeInfo buildMyInfo()
    {
        meshtastic_MyNodeInfo info = meshtastic_MyNodeInfo_init_zero;
        info.my_node_num = ctx_.getSelfNodeId();
        info.reboot_count = 0;
        info.min_app_version = kOfficialMinAppVersion;
        size_t count = node_entries_.size() + 1;
        if (count > std::numeric_limits<uint16_t>::max())
        {
            count = std::numeric_limits<uint16_t>::max();
        }
        info.nodedb_count = static_cast<uint16_t>(count);

        const uint64_t mac = ESP.getEfuseMac();
        const uint64_t derived =
            mac ^ (static_cast<uint64_t>(ctx_.getSelfNodeId()) << 16) ^ 0x544D455348424C45ULL;
        memcpy(info.device_id.bytes, &mac, sizeof(mac));
        memcpy(info.device_id.bytes + sizeof(mac), &derived, sizeof(derived));
        info.device_id.size = 16;

        // Use a human-friendly environment name while keeping firmware_version
        // semver-compatible for the official mobile apps.
        strncpy(info.pio_env, "Trail Mate", sizeof(info.pio_env) - 1);
        info.firmware_edition = meshtastic_FirmwareEdition_VANILLA;
        return info;
    }

    meshtastic_HardwareModel getSelfHardwareModel()
    {
#if defined(ARDUINO_T_DECK)
        return meshtastic_HardwareModel_T_DECK;
#elif defined(ARDUINO_LILYGO_TWATCH_S3)
        return meshtastic_HardwareModel_T_WATCH_S3;
#elif defined(ARDUINO_M5STACK_TAB5)
        return meshtastic_HardwareModel_MESH_TAB;
#elif defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_SX1280)
        return meshtastic_HardwareModel_T_LORA_PAGER;
#else
        return meshtastic_HardwareModel_UNSET;
#endif
    }

    meshtastic_DeviceMetadata buildMetadata()
    {
        meshtastic_DeviceMetadata meta = meshtastic_DeviceMetadata_init_zero;
        strncpy(meta.firmware_version, kCompatFirmwareVersion, sizeof(meta.firmware_version) - 1);
        meta.device_state_version = kOfficialDeviceStateVersion;
        meta.canShutdown = true;
        meta.hasWifi = false;
        meta.hasBluetooth = true;
        meta.hasEthernet = false;
        meta.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
        meta.position_flags = 0;
        meta.hw_model = getSelfHardwareModel();
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

    chat::meshtastic::MtAdapter* meshtasticBackend() const
    {
        auto* adapter = ctx_.getMeshAdapter();
        if (!adapter)
        {
            return nullptr;
        }
        auto* router = static_cast<chat::MeshAdapterRouter*>(adapter);
        if (!router || router->backendProtocol() != chat::MeshProtocol::Meshtastic)
        {
            return nullptr;
        }
        return static_cast<chat::meshtastic::MtAdapter*>(
            router->backendForProtocol(router->backendProtocol()));
    }

    void fillNodePublicKey(uint32_t node_id, meshtastic_User& user)
    {
        auto* mt = meshtasticBackend();
        if (!mt)
        {
            return;
        }
        uint8_t key[32];
        if (!mt->getNodePublicKey(node_id, key))
        {
            return;
        }
        user.public_key.size = 32;
        memcpy(user.public_key.bytes, key, sizeof(key));
    }

    void fillOwnPublicKey(meshtastic_User& user)
    {
        auto* mt = meshtasticBackend();
        if (!mt)
        {
            return;
        }
        uint8_t key[32];
        if (!mt->getOwnPublicKey(key))
        {
            return;
        }
        user.public_key.size = 32;
        memcpy(user.public_key.bytes, key, sizeof(key));
    }

    meshtastic_DeviceMetrics buildDeviceMetrics()
    {
        meshtastic_DeviceMetrics m = meshtastic_DeviceMetrics_init_zero;
        if (BoardBase* board = ctx_.getBoard())
        {
            int level = board->getBatteryLevel();
            if (level >= 0 && level <= 100)
            {
                m.has_battery_level = true;
                m.battery_level = static_cast<uint32_t>(level);
            }
        }
        m.has_uptime_seconds = true;
        m.uptime_seconds = static_cast<uint32_t>(millis() / 1000U);
        return m;
    }

    meshtastic_LocalStats buildLocalStats()
    {
        meshtastic_LocalStats stats = meshtastic_LocalStats_init_zero;
        stats.uptime_seconds = static_cast<uint32_t>(millis() / 1000U);
        stats.channel_utilization = 0.0f;
        stats.air_util_tx = 0.0f;
        stats.num_packets_tx = 0;
        stats.num_packets_rx = 0;
        stats.num_packets_rx_bad = 0;
        stats.num_rx_dupe = 0;
        stats.num_tx_relay = 0;
        stats.num_tx_relay_canceled = 0;
        stats.num_tx_dropped = 0;
        stats.heap_total_bytes = ESP.getHeapSize();
        stats.heap_free_bytes = ESP.getFreeHeap();
        if (auto* store = ctx_.getNodeStore())
        {
            size_t total = store->getEntries().size();
            stats.num_total_nodes = static_cast<uint16_t>(std::min<size_t>(total, 0xFFFF));
            stats.num_online_nodes = stats.num_total_nodes;
        }
        return stats;
    }

    bool buildSelfPositionPayload(uint8_t* out_buf, size_t* out_len) const
    {
        if (!out_buf || !out_len || *out_len == 0)
        {
            return false;
        }

        meshtastic_Position pos = meshtastic_Position_init_zero;
        const uint32_t self = ctx_.getSelfNodeId();
        bool have_position = false;
        if (self != 0)
        {
            if (const auto* info = ctx_.getContactService().getNodeInfo(self))
            {
                if (info->position.valid)
                {
                    pos.has_latitude_i = true;
                    pos.latitude_i = info->position.latitude_i;
                    pos.has_longitude_i = true;
                    pos.longitude_i = info->position.longitude_i;
                    pos.location_source = meshtastic_Position_LocSource_LOC_INTERNAL;
                    if (info->position.has_altitude)
                    {
                        pos.has_altitude = true;
                        pos.altitude = info->position.altitude;
                        pos.altitude_source = meshtastic_Position_AltSource_ALT_INTERNAL;
                    }
                    if (info->position.timestamp != 0)
                    {
                        pos.timestamp = info->position.timestamp;
                    }
                    if (info->position.precision_bits != 0)
                    {
                        pos.precision_bits = info->position.precision_bits;
                    }
                    if (info->position.pdop != 0)
                    {
                        pos.PDOP = info->position.pdop;
                    }
                    if (info->position.hdop != 0)
                    {
                        pos.HDOP = info->position.hdop;
                    }
                    if (info->position.vdop != 0)
                    {
                        pos.VDOP = info->position.vdop;
                    }
                    if (info->position.gps_accuracy_mm != 0)
                    {
                        pos.gps_accuracy = info->position.gps_accuracy_mm;
                    }
                    have_position = true;
                }
            }
        }

        if (!have_position)
        {
            gps::GpsState gps_state = gps::gps_get_data();
            if (!gps_state.valid)
            {
                return false;
            }

            pos.has_latitude_i = true;
            pos.latitude_i = static_cast<int32_t>(gps_state.lat * 1e7);
            pos.has_longitude_i = true;
            pos.longitude_i = static_cast<int32_t>(gps_state.lng * 1e7);
            pos.location_source = meshtastic_Position_LocSource_LOC_INTERNAL;
            if (gps_state.has_alt)
            {
                pos.has_altitude = true;
                pos.altitude = static_cast<int32_t>(std::lround(gps_state.alt_m));
                pos.altitude_source = meshtastic_Position_AltSource_ALT_INTERNAL;
            }
            if (gps_state.has_speed)
            {
                pos.has_ground_speed = true;
                pos.ground_speed = static_cast<uint32_t>(std::lround(gps_state.speed_mps));
            }
            if (gps_state.has_course)
            {
                double course = gps_state.course_deg;
                if (course < 0.0)
                {
                    course = 0.0;
                }
                uint32_t cdeg = static_cast<uint32_t>(std::lround(course * 100.0));
                if (cdeg >= 36000U)
                {
                    cdeg = 35999U;
                }
                pos.has_ground_track = true;
                pos.ground_track = cdeg;
            }
            if (gps_state.satellites > 0)
            {
                pos.sats_in_view = gps_state.satellites;
            }
        }

        uint32_t ts = static_cast<uint32_t>(time(nullptr));
        if (pos.timestamp == 0 && ts >= 1577836800U)
        {
            pos.timestamp = ts;
        }
        if (pos.time == 0 && ts >= 1577836800U)
        {
            pos.time = ts;
        }

        pb_ostream_t stream = pb_ostream_from_buffer(out_buf, *out_len);
        if (!pb_encode(&stream, meshtastic_Position_fields, &pos))
        {
            return false;
        }

        *out_len = stream.bytes_written;
        return true;
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
        channel.settings.uplink_enabled = (idx == 0) ? cfg.primary_uplink_enabled : cfg.secondary_uplink_enabled;
        channel.settings.downlink_enabled = (idx == 0) ? cfg.primary_downlink_enabled : cfg.secondary_downlink_enabled;
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
            out.payload_variant.lora.ignore_mqtt = cfg.meshtastic_config.ignore_mqtt;
            out.payload_variant.lora.config_ok_to_mqtt = cfg.meshtastic_config.config_ok_to_mqtt;
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

    bool configTypeFromConfigVariant(pb_size_t variant_tag, meshtastic_AdminMessage_ConfigType& out)
    {
        switch (variant_tag)
        {
        case meshtastic_Config_device_tag:
            out = meshtastic_AdminMessage_ConfigType_DEVICE_CONFIG;
            return true;
        case meshtastic_Config_position_tag:
            out = meshtastic_AdminMessage_ConfigType_POSITION_CONFIG;
            return true;
        case meshtastic_Config_power_tag:
            out = meshtastic_AdminMessage_ConfigType_POWER_CONFIG;
            return true;
        case meshtastic_Config_network_tag:
            out = meshtastic_AdminMessage_ConfigType_NETWORK_CONFIG;
            return true;
        case meshtastic_Config_display_tag:
            out = meshtastic_AdminMessage_ConfigType_DISPLAY_CONFIG;
            return true;
        case meshtastic_Config_lora_tag:
            out = meshtastic_AdminMessage_ConfigType_LORA_CONFIG;
            return true;
        case meshtastic_Config_bluetooth_tag:
            out = meshtastic_AdminMessage_ConfigType_BLUETOOTH_CONFIG;
            return true;
        case meshtastic_Config_security_tag:
            out = meshtastic_AdminMessage_ConfigType_SECURITY_CONFIG;
            return true;
        case meshtastic_Config_sessionkey_tag:
            out = meshtastic_AdminMessage_ConfigType_SESSIONKEY_CONFIG;
            return true;
        case meshtastic_Config_device_ui_tag:
            out = meshtastic_AdminMessage_ConfigType_DEVICEUI_CONFIG;
            return true;
        default:
            return false;
        }
    }

    bool moduleConfigTypeFromVariant(pb_size_t variant_tag, meshtastic_AdminMessage_ModuleConfigType& out)
    {
        switch (variant_tag)
        {
        case meshtastic_ModuleConfig_mqtt_tag:
            out = meshtastic_AdminMessage_ModuleConfigType_MQTT_CONFIG;
            return true;
        case meshtastic_ModuleConfig_serial_tag:
            out = meshtastic_AdminMessage_ModuleConfigType_SERIAL_CONFIG;
            return true;
        case meshtastic_ModuleConfig_external_notification_tag:
            out = meshtastic_AdminMessage_ModuleConfigType_EXTNOTIF_CONFIG;
            return true;
        case meshtastic_ModuleConfig_store_forward_tag:
            out = meshtastic_AdminMessage_ModuleConfigType_STOREFORWARD_CONFIG;
            return true;
        case meshtastic_ModuleConfig_range_test_tag:
            out = meshtastic_AdminMessage_ModuleConfigType_RANGETEST_CONFIG;
            return true;
        case meshtastic_ModuleConfig_telemetry_tag:
            out = meshtastic_AdminMessage_ModuleConfigType_TELEMETRY_CONFIG;
            return true;
        case meshtastic_ModuleConfig_canned_message_tag:
            out = meshtastic_AdminMessage_ModuleConfigType_CANNEDMSG_CONFIG;
            return true;
        case meshtastic_ModuleConfig_audio_tag:
            out = meshtastic_AdminMessage_ModuleConfigType_AUDIO_CONFIG;
            return true;
        case meshtastic_ModuleConfig_remote_hardware_tag:
            out = meshtastic_AdminMessage_ModuleConfigType_REMOTEHARDWARE_CONFIG;
            return true;
        case meshtastic_ModuleConfig_neighbor_info_tag:
            out = meshtastic_AdminMessage_ModuleConfigType_NEIGHBORINFO_CONFIG;
            return true;
        case meshtastic_ModuleConfig_ambient_lighting_tag:
            out = meshtastic_AdminMessage_ModuleConfigType_AMBIENTLIGHTING_CONFIG;
            return true;
        case meshtastic_ModuleConfig_detection_sensor_tag:
            out = meshtastic_AdminMessage_ModuleConfigType_DETECTIONSENSOR_CONFIG;
            return true;
        case meshtastic_ModuleConfig_paxcounter_tag:
            out = meshtastic_AdminMessage_ModuleConfigType_PAXCOUNTER_CONFIG;
            return true;
        default:
            return false;
        }
    }

    meshtastic_NodeInfo buildNodeInfo(const chat::contacts::NodeEntry& entry)
    {
        meshtastic_NodeInfo info = meshtastic_NodeInfo_init_zero;
        info.num = entry.node_id;
        info.has_user = true;
        zeroInit(info.user);

        // Start from stored names in NodeStore.
        char long_name[sizeof(entry.long_name)];
        char short_name[sizeof(entry.short_name)];
        strncpy(long_name, entry.long_name, sizeof(long_name) - 1);
        long_name[sizeof(long_name) - 1] = '\0';
        strncpy(short_name, entry.short_name, sizeof(short_name) - 1);
        short_name[sizeof(short_name) - 1] = '\0';

        // Ensure we always have a usable short name (fallback to node_id suffix).
        if (short_name[0] == '\0')
        {
            snprintf(short_name, sizeof(short_name), "%04X",
                     static_cast<unsigned>(entry.node_id & 0xFFFF));
        }

        // If long name is empty, fall back to a lilygo-XXXX style name.
        if (long_name[0] == '\0')
        {
            snprintf(long_name, sizeof(long_name), "lilygo-%04X",
                     static_cast<unsigned>(entry.node_id & 0xFFFF));
        }

        strncpy(info.user.long_name, long_name, sizeof(info.user.long_name) - 1);
        strncpy(info.user.short_name, short_name, sizeof(info.user.short_name) - 1);
        fillUserId(info.user.id, sizeof(info.user.id), entry.node_id);
        fillNodePublicKey(entry.node_id, info.user);

        // Use the stored hardware model if we have it; otherwise leave UNSET so
        // the Meshtastic app can decide how to render unknown peers.
        info.user.hw_model = static_cast<meshtastic_HardwareModel>(entry.hw_model);
        info.user.role = roleFromConfig(entry.role);
        info.channel = (entry.channel == 0xFF) ? 0 : entry.channel;
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
        char long_name[sizeof(ctx_.getConfig().node_name)];
        char short_name[sizeof(ctx_.getConfig().short_name)];
        ctx_.getEffectiveUserInfo(long_name, sizeof(long_name), short_name, sizeof(short_name));
        strncpy(info.user.long_name, long_name, sizeof(info.user.long_name) - 1);
        strncpy(info.user.short_name, short_name, sizeof(info.user.short_name) - 1);
        fillUserId(info.user.id, sizeof(info.user.id), info.num);
        fillOwnPublicKey(info.user);
        info.user.hw_model = getSelfHardwareModel();
        info.user.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
        info.channel = 0;
        info.last_heard = nowSeconds();
        info.has_hops_away = true;
        info.hops_away = 0;
        info.is_favorite = true;
        info.is_ignored = false;
        info.is_key_manually_verified = false;
        info.has_device_metrics = true;
        info.device_metrics = buildDeviceMetrics();
        return info;
    }

    meshtastic_MeshPacket buildPacketFromText(const chat::MeshIncomingText& msg)
    {
        meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
        packet.from = msg.from;
        packet.to = msg.to;
        packet.channel = channelIndexFromId(msg.channel);
        packet.id = normalizePacketId(msg.msg_id);
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
        packet.id = normalizePacketId(msg.packet_id);
        packet.rx_time = (msg.rx_meta.rx_timestamp_s != 0) ? msg.rx_meta.rx_timestamp_s : nowSeconds();
        packet.rx_snr = msg.rx_meta.snr_db_x10 / 10.0f;
        packet.rx_rssi = msg.rx_meta.rssi_dbm_x10 / 10;
        packet.hop_limit = msg.hop_limit;
        packet.relay_node = msg.rx_meta.relay_node;
        packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        zeroInit(packet.decoded);
        packet.decoded.portnum = static_cast<meshtastic_PortNum>(msg.portnum);
        packet.decoded.dest = msg.to;
        packet.decoded.source = msg.from;
        packet.decoded.request_id = msg.request_id;
        packet.decoded.want_response = msg.want_response;
        packet.decoded.has_bitfield = true;
        packet.decoded.bitfield = 0;
        size_t len = std::min(msg.payload.size(), sizeof(packet.decoded.payload.bytes));
        packet.decoded.payload.size = static_cast<pb_size_t>(len);
        memcpy(packet.decoded.payload.bytes, msg.payload.data(), len);
        return packet;
    }

    bool handleLocalSelfPacket(meshtastic_MeshPacket& packet)
    {
        const uint32_t self = ctx_.getSelfNodeId();
        if (self == 0 || packet.to != self ||
            packet.which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        {
            return false;
        }

        if (packet.decoded.portnum == meshtastic_PortNum_TELEMETRY_APP &&
            packet.decoded.want_response && packet.decoded.payload.size > 0)
        {
            meshtastic_Telemetry req = meshtastic_Telemetry_init_zero;
            pb_istream_t req_stream =
                pb_istream_from_buffer(packet.decoded.payload.bytes, packet.decoded.payload.size);
            if (!pb_decode(&req_stream, meshtastic_Telemetry_fields, &req))
            {
                ble_log("self telemetry decode fail req_id=%lu", static_cast<unsigned long>(packet.id));
                return false;
            }

            meshtastic_Telemetry resp = meshtastic_Telemetry_init_zero;
            resp.time = nowSeconds();
            switch (req.which_variant)
            {
            case meshtastic_Telemetry_device_metrics_tag:
                resp.which_variant = meshtastic_Telemetry_device_metrics_tag;
                resp.variant.device_metrics = buildDeviceMetrics();
                break;
            case meshtastic_Telemetry_local_stats_tag:
                resp.which_variant = meshtastic_Telemetry_local_stats_tag;
                resp.variant.local_stats = buildLocalStats();
                break;
            default:
                ble_log("self telemetry unsupported variant=%u req_id=%lu",
                        static_cast<unsigned>(req.which_variant),
                        static_cast<unsigned long>(packet.id));
                return false;
            }

            uint8_t payload[meshtastic_Telemetry_size];
            pb_ostream_t out_stream = pb_ostream_from_buffer(payload, sizeof(payload));
            if (!pb_encode(&out_stream, meshtastic_Telemetry_fields, &resp))
            {
                return false;
            }

            meshtastic_MeshPacket reply = meshtastic_MeshPacket_init_zero;
            reply.from = self;
            reply.to = self;
            reply.channel = packet.channel;
            reply.id = normalizePacketId(0);
            reply.rx_time = nowSeconds();
            reply.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
            zeroInit(reply.decoded);
            reply.decoded.portnum = meshtastic_PortNum_TELEMETRY_APP;
            reply.decoded.dest = self;
            reply.decoded.source = self;
            reply.decoded.request_id = packet.id;
            reply.decoded.want_response = false;
            reply.decoded.has_bitfield = true;
            reply.decoded.bitfield = 0;
            reply.decoded.payload.size = static_cast<pb_size_t>(out_stream.bytes_written);
            memcpy(reply.decoded.payload.bytes, payload, out_stream.bytes_written);
            packet_queue_.push_back(reply);

            ble_log("self telemetry reply req_id=%lu resp_id=%lu variant=%u",
                    static_cast<unsigned long>(packet.id),
                    static_cast<unsigned long>(reply.id),
                    static_cast<unsigned>(resp.which_variant));
            return true;
        }

        if (packet.decoded.portnum == meshtastic_PortNum_POSITION_APP && packet.decoded.want_response)
        {
            uint8_t payload[meshtastic_Position_size];
            size_t payload_len = sizeof(payload);
            if (!buildSelfPositionPayload(payload, &payload_len))
            {
                ble_log("self position unavailable req_id=%lu", static_cast<unsigned long>(packet.id));
                return false;
            }

            meshtastic_MeshPacket reply = meshtastic_MeshPacket_init_zero;
            reply.from = self;
            reply.to = self;
            reply.channel = packet.channel;
            reply.id = normalizePacketId(0);
            reply.rx_time = nowSeconds();
            reply.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
            zeroInit(reply.decoded);
            reply.decoded.portnum = meshtastic_PortNum_POSITION_APP;
            reply.decoded.dest = self;
            reply.decoded.source = self;
            reply.decoded.request_id = packet.id;
            reply.decoded.want_response = false;
            reply.decoded.has_bitfield = true;
            reply.decoded.bitfield = 0;
            reply.decoded.payload.size = static_cast<pb_size_t>(payload_len);
            memcpy(reply.decoded.payload.bytes, payload, payload_len);
            packet_queue_.push_back(reply);

            ble_log("self position reply req_id=%lu resp_id=%lu len=%u",
                    static_cast<unsigned long>(packet.id),
                    static_cast<unsigned long>(reply.id),
                    static_cast<unsigned>(payload_len));
            return true;
        }

        if (packet.decoded.portnum == meshtastic_PortNum_NODEINFO_APP && packet.decoded.want_response)
        {
            meshtastic_NodeInfo self_info = buildSelfNodeInfo();
            uint8_t payload[sizeof(self_info.user)];
            pb_ostream_t out_stream = pb_ostream_from_buffer(payload, sizeof(payload));
            if (!pb_encode(&out_stream, meshtastic_User_fields, &self_info.user))
            {
                return false;
            }

            meshtastic_MeshPacket reply = meshtastic_MeshPacket_init_zero;
            reply.from = self;
            reply.to = self;
            reply.channel = packet.channel;
            reply.id = normalizePacketId(0);
            reply.rx_time = nowSeconds();
            reply.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
            zeroInit(reply.decoded);
            reply.decoded.portnum = meshtastic_PortNum_NODEINFO_APP;
            reply.decoded.dest = self;
            reply.decoded.source = self;
            reply.decoded.request_id = packet.id;
            reply.decoded.want_response = false;
            reply.decoded.has_bitfield = true;
            reply.decoded.bitfield = 0;
            reply.decoded.payload.size = static_cast<pb_size_t>(out_stream.bytes_written);
            memcpy(reply.decoded.payload.bytes, payload, out_stream.bytes_written);
            packet_queue_.push_back(reply);

            ble_log("self nodeinfo reply req_id=%lu resp_id=%lu",
                    static_cast<unsigned long>(packet.id),
                    static_cast<unsigned long>(reply.id));
            return true;
        }

        return false;
    }

    bool handleToRadioPacket(meshtastic_MeshPacket& packet)
    {
        if (packet.which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        {
            return false;
        }
        if (packet.id == 0)
        {
            packet.id = normalizePacketId(0);
        }
        packet.from = ctx_.getSelfNodeId();
        packet.next_hop = 0;
        packet.relay_node = 0;
        packet.rx_time = nowSeconds();
        if (wasSeenRecently(packet.id))
        {
            ble_log("drop duplicate packet id=%lu", static_cast<unsigned long>(packet.id));
            return false;
        }
        const bool admin_for_self =
            (packet.decoded.portnum == meshtastic_PortNum_ADMIN_APP) &&
            (packet.to == 0 || packet.to == ctx_.getSelfNodeId());
        if (admin_for_self)
        {
            bool ok = handleAdmin(packet);
            enqueueQueueStatus(packet.id, ok);
            return ok;
        }

        if (handleLocalSelfPacket(packet))
        {
            enqueueQueueStatus(packet.id, true);
            return true;
        }

        auto* adapter = ctx_.getMeshAdapter();
        if (!adapter)
        {
            enqueueQueueStatus(packet.id, false);
            return false;
        }

        auto* router = static_cast<chat::MeshAdapterRouter*>(adapter);
        if (router && router->backendProtocol() == chat::MeshProtocol::Meshtastic)
        {
            if (auto* backend = router->backendForProtocol(router->backendProtocol()))
            {
                auto* mt = static_cast<chat::meshtastic::MtAdapter*>(backend);
                if (mt)
                {
                    bool ok = mt->sendMeshPacket(packet);
                    meshtastic_Routing_Error routing_error = mt->getLastRoutingError();
                    ble_log("ToRadio raw port=%u to=%08lX ch=%u pki=%u want_resp=%u want_ack=%u ok=%u err=%u",
                            static_cast<unsigned>(packet.decoded.portnum),
                            static_cast<unsigned long>(packet.to),
                            static_cast<unsigned>(packet.channel),
                            packet.pki_encrypted ? 1U : 0U,
                            packet.decoded.want_response ? 1U : 0U,
                            packet.want_ack ? 1U : 0U,
                            ok ? 1U : 0U,
                            static_cast<unsigned>(routing_error));
                    if (!ok && routing_error != meshtastic_Routing_Error_NONE)
                    {
                        enqueueRoutingAck(packet, routing_error);
                    }
                    enqueueQueueStatus(packet.id, ok);
                    return ok;
                }
            }
        }

        chat::ChannelId ch = channelIdFromIndex(packet.channel);
        if (packet.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP)
        {
            std::string text(reinterpret_cast<char*>(packet.decoded.payload.bytes),
                             packet.decoded.payload.size);
            chat::MessageId msg_id = 0;
            bool ok = adapter->sendText(ch, text, &msg_id, packet.to);
            ble_log("ToRadio text to=%08lX len=%u ok=%u",
                    static_cast<unsigned long>(packet.to),
                    static_cast<unsigned>(packet.decoded.payload.size),
                    ok ? 1U : 0U);
            enqueueQueueStatus(packet.id, ok);
            return ok;
        }

        bool ok = adapter->sendAppData(ch,
                                       static_cast<uint32_t>(packet.decoded.portnum),
                                       packet.decoded.payload.bytes,
                                       packet.decoded.payload.size,
                                       packet.to,
                                       packet.want_ack,
                                       packet.id,
                                       packet.decoded.want_response);
        ble_log("ToRadio app port=%u to=%08lX len=%u want_resp=%u want_ack=%u ok=%u",
                static_cast<unsigned>(packet.decoded.portnum),
                static_cast<unsigned long>(packet.to),
                static_cast<unsigned>(packet.decoded.payload.size),
                packet.decoded.want_response ? 1U : 0U,
                packet.want_ack ? 1U : 0U,
                ok ? 1U : 0U);
        enqueueQueueStatus(packet.id, ok);
        return ok;
    }

    bool handleAdmin(meshtastic_MeshPacket& packet)
    {
        meshtastic_AdminMessage req = meshtastic_AdminMessage_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(packet.decoded.payload.bytes,
                                                     packet.decoded.payload.size);
        if (!pb_decode(&stream, meshtastic_AdminMessage_fields, &req))
        {
            ble_log("Admin decode failed (len=%u)", (unsigned)packet.decoded.payload.size);
            return false;
        }

        meshtastic_AdminMessage resp = meshtastic_AdminMessage_init_zero;
        bool has_resp = false;
        bool unsupported = false;

        ble_log("Admin request tag=%d id=%lu from=%08lX to=%08lX",
                (int)req.which_payload_variant,
                static_cast<unsigned long>(packet.id),
                static_cast<unsigned long>(packet.from),
                static_cast<unsigned long>(packet.to));

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
        case meshtastic_AdminMessage_get_canned_message_module_messages_request_tag:
            resp.which_payload_variant = meshtastic_AdminMessage_get_canned_message_module_messages_response_tag;
            copyBounded(resp.get_canned_message_module_messages_response,
                        sizeof(resp.get_canned_message_module_messages_response),
                        admin_canned_messages_);
            has_resp = true;
            break;
        case meshtastic_AdminMessage_get_device_metadata_request_tag:
            resp.which_payload_variant = meshtastic_AdminMessage_get_device_metadata_response_tag;
            resp.get_device_metadata_response = buildMetadata();
            has_resp = true;
            break;
        case meshtastic_AdminMessage_get_ringtone_request_tag:
            resp.which_payload_variant = meshtastic_AdminMessage_get_ringtone_response_tag;
            copyBounded(resp.get_ringtone_response,
                        sizeof(resp.get_ringtone_response),
                        admin_ringtone_);
            has_resp = true;
            break;
        case meshtastic_AdminMessage_get_device_connection_status_request_tag:
        {
            resp.which_payload_variant = meshtastic_AdminMessage_get_device_connection_status_response_tag;
            zeroInit(resp.get_device_connection_status_response);
            resp.get_device_connection_status_response.has_bluetooth = true;
            resp.get_device_connection_status_response.bluetooth.pin = service_.ble_config_.fixed_pin;
            resp.get_device_connection_status_response.bluetooth.rssi = 0;
            resp.get_device_connection_status_response.bluetooth.is_connected =
                service_.server_ ? (service_.server_->getConnectedCount() > 0) : false;
            has_resp = true;
            break;
        }
        case meshtastic_AdminMessage_get_ui_config_request_tag:
            resp.which_payload_variant = meshtastic_AdminMessage_get_ui_config_response_tag;
            resp.get_ui_config_response = buildDeviceUi();
            has_resp = true;
            break;
        case meshtastic_AdminMessage_set_owner_tag:
        {
            auto& cfg = ctx_.getConfig();
            ble_log("set_owner long='%s' short='%s'",
                    req.set_owner.long_name,
                    req.set_owner.short_name);
            copyBounded(cfg.node_name, sizeof(cfg.node_name), req.set_owner.long_name);
            copyBounded(cfg.short_name, sizeof(cfg.short_name), req.set_owner.short_name);
            ctx_.saveConfig();
            ctx_.applyUserInfo();

            // Per Meshtastic behavior, acknowledge set_owner by returning the
            // updated owner User record, same shape as get_owner_response.
            resp.which_payload_variant = meshtastic_AdminMessage_get_owner_response_tag;
            resp.get_owner_response = buildSelfNodeInfo().user;
            ble_log("set_owner applied, responding with owner long='%s' short='%s'",
                    resp.get_owner_response.long_name,
                    resp.get_owner_response.short_name);
            has_resp = true;
            break;
        }
        case meshtastic_AdminMessage_set_channel_tag:
        {
            auto& cfg = ctx_.getConfig();
            if (req.set_channel.index == 0)
            {
                cfg.primary_enabled = (req.set_channel.role != meshtastic_Channel_Role_DISABLED);
                cfg.primary_uplink_enabled = req.set_channel.settings.uplink_enabled;
                cfg.primary_downlink_enabled = req.set_channel.settings.downlink_enabled;
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
                cfg.secondary_uplink_enabled = req.set_channel.settings.uplink_enabled;
                cfg.secondary_downlink_enabled = req.set_channel.settings.downlink_enabled;
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
            resp.which_payload_variant = meshtastic_AdminMessage_get_channel_response_tag;
            resp.get_channel_response = buildChannel(req.set_channel.index);
            has_resp = true;
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
                cfg.meshtastic_config.ignore_mqtt = lora.ignore_mqtt;
                cfg.meshtastic_config.config_ok_to_mqtt = lora.config_ok_to_mqtt;
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
                ctx_.applyPositionConfig();
                break;
            }
            case meshtastic_Config_display_tag:
            {
                uint32_t secs = req.set_config.payload_variant.display.screen_on_secs;
                if (secs > 0)
                {
                    setScreenSleepTimeout(clampScreenTimeoutMs(secs * 1000U));
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
                    setScreenSleepTimeout(clampScreenTimeoutMs(static_cast<uint32_t>(ui.screen_timeout) * 1000U));
                }
                break;
            }
            default:
                break;
            }
            resp.which_payload_variant = meshtastic_AdminMessage_get_config_response_tag;
            meshtastic_AdminMessage_ConfigType config_type = meshtastic_AdminMessage_ConfigType_DEVICE_CONFIG;
            if (configTypeFromConfigVariant(req.set_config.which_payload_variant, config_type))
            {
                resp.get_config_response = buildConfig(config_type);
            }
            else
            {
                resp.get_config_response = req.set_config;
                ble_log("set_config response fallback variant=%u",
                        static_cast<unsigned>(req.set_config.which_payload_variant));
            }
            has_resp = true;
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
            resp.which_payload_variant = meshtastic_AdminMessage_get_module_config_response_tag;
            meshtastic_AdminMessage_ModuleConfigType module_type =
                meshtastic_AdminMessage_ModuleConfigType_MQTT_CONFIG;
            if (moduleConfigTypeFromVariant(req.set_module_config.which_payload_variant, module_type))
            {
                resp.get_module_config_response = buildModuleConfig(module_type);
            }
            else
            {
                resp.get_module_config_response = req.set_module_config;
                ble_log("set_module_config response fallback variant=%u",
                        static_cast<unsigned>(req.set_module_config.which_payload_variant));
            }
            has_resp = true;
            break;
        }
        case meshtastic_AdminMessage_set_canned_message_module_messages_tag:
        {
            copyBounded(admin_canned_messages_,
                        sizeof(admin_canned_messages_),
                        req.set_canned_message_module_messages);
            resp.which_payload_variant = meshtastic_AdminMessage_get_canned_message_module_messages_response_tag;
            copyBounded(resp.get_canned_message_module_messages_response,
                        sizeof(resp.get_canned_message_module_messages_response),
                        admin_canned_messages_);
            has_resp = true;
            break;
        }
        case meshtastic_AdminMessage_set_ringtone_message_tag:
        {
            copyBounded(admin_ringtone_,
                        sizeof(admin_ringtone_),
                        req.set_ringtone_message);
            resp.which_payload_variant = meshtastic_AdminMessage_get_ringtone_response_tag;
            copyBounded(resp.get_ringtone_response,
                        sizeof(resp.get_ringtone_response),
                        admin_ringtone_);
            has_resp = true;
            break;
        }
        case meshtastic_AdminMessage_store_ui_config_tag:
        {
            const auto& ui = req.store_ui_config;
            if (ui.screen_timeout > 0)
            {
                setScreenSleepTimeout(clampScreenTimeoutMs(static_cast<uint32_t>(ui.screen_timeout) * 1000U));
            }
            resp.which_payload_variant = meshtastic_AdminMessage_get_ui_config_response_tag;
            resp.get_ui_config_response = buildDeviceUi();
            has_resp = true;
            break;
        }
        case meshtastic_AdminMessage_set_time_only_tag:
        {
            struct timeval tv = {};
            tv.tv_sec = static_cast<time_t>(req.set_time_only);
            tv.tv_usec = 0;
            const bool ok = (settimeofday(&tv, nullptr) == 0);
            ble_log("set_time_only=%lu ok=%u",
                    static_cast<unsigned long>(req.set_time_only),
                    ok ? 1U : 0U);
            break;
        }
        case meshtastic_AdminMessage_remove_by_nodenum_tag:
        {
            const uint32_t node_num = req.remove_by_nodenum;
            bool removed = false;
            if (node_num != 0)
            {
                removed = ctx_.getContactService().removeNode(node_num);

                auto* adapter = ctx_.getMeshAdapter();
                auto* router = static_cast<chat::MeshAdapterRouter*>(adapter);
                if (router && router->backendProtocol() == chat::MeshProtocol::Meshtastic)
                {
                    if (auto* backend = router->backendForProtocol(router->backendProtocol()))
                    {
                        auto* mt = static_cast<chat::meshtastic::MtAdapter*>(backend);
                        if (mt)
                        {
                            mt->forgetNodePublicKey(node_num);
                        }
                    }
                }
            }
            ble_log("remove_by_nodenum node=%08lX removed=%u",
                    static_cast<unsigned long>(node_num),
                    removed ? 1U : 0U);
            break;
        }
        case meshtastic_AdminMessage_add_contact_tag:
        {
            const auto& contact = req.add_contact;
            const auto& user = contact.user;
            ble_log("add_contact node=%08lX short='%s' long='%s' verified=%u ignore=%u key=%u",
                    static_cast<unsigned long>(contact.node_num),
                    user.short_name,
                    user.long_name,
                    contact.manually_verified ? 1U : 0U,
                    contact.should_ignore ? 1U : 0U,
                    static_cast<unsigned>(user.public_key.size));

            if (contact.node_num != 0)
            {
                ctx_.getContactService().updateNodeInfo(
                    contact.node_num,
                    user.short_name,
                    user.long_name,
                    0.0f,
                    0.0f,
                    nowSeconds(),
                    static_cast<uint8_t>(chat::contacts::NodeProtocolType::Meshtastic));

                auto* adapter = ctx_.getMeshAdapter();
                auto* router = static_cast<chat::MeshAdapterRouter*>(adapter);
                if (router && router->backendProtocol() == chat::MeshProtocol::Meshtastic)
                {
                    if (auto* backend = router->backendForProtocol(router->backendProtocol()))
                    {
                        auto* mt = static_cast<chat::meshtastic::MtAdapter*>(backend);
                        if (mt && user.public_key.size == 32)
                        {
                            mt->rememberNodePublicKey(contact.node_num,
                                                      user.public_key.bytes,
                                                      user.public_key.size);
                        }
                    }
                }
            }
            break;
        }
        default:
            ble_log("Admin request unsupported tag=%d", static_cast<int>(req.which_payload_variant));
            unsupported = true;
            break;
        }

        if (has_resp)
        {
            return sendAdminResponse(resp, packet);
        }
        // Keep legacy behavior for unsupported/no-op commands while still emitting routing status above.
        return true;
    }

    void enqueueQueueStatus(uint32_t mesh_packet_id, bool success)
    {
        meshtastic_QueueStatus status = buildQueueStatus(mesh_packet_id);
        status.res = success ? 0 : 1;
        queue_status_queue_.push_back(status);
        ble_log("queue status id=%lu res=%u q=%u",
                static_cast<unsigned long>(mesh_packet_id),
                static_cast<unsigned>(status.res),
                static_cast<unsigned>(queue_status_queue_.size()));
        if (state_ == State::SendPackets)
        {
            service_.notifyFromNum(0);
        }
    }

    bool enqueueRoutingAck(const meshtastic_MeshPacket& request, meshtastic_Routing_Error reason)
    {
        meshtastic_Routing routing = meshtastic_Routing_init_default;
        routing.which_variant = meshtastic_Routing_error_reason_tag;
        routing.error_reason = reason;

        uint8_t payload[64];
        pb_ostream_t ostream = pb_ostream_from_buffer(payload, sizeof(payload));
        if (!pb_encode(&ostream, meshtastic_Routing_fields, &routing))
        {
            return false;
        }

        meshtastic_MeshPacket ack = meshtastic_MeshPacket_init_zero;
        ack.from = ctx_.getSelfNodeId();
        ack.to = request.from;
        ack.channel = request.channel;
        ack.id = normalizePacketId(0);
        ack.rx_time = nowSeconds();
        ack.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        zeroInit(ack.decoded);
        ack.decoded.portnum = meshtastic_PortNum_ROUTING_APP;
        ack.decoded.dest = ack.to;
        ack.decoded.source = ack.from;
        ack.decoded.request_id = request.id;
        ack.decoded.want_response = false;
        ack.decoded.payload.size = static_cast<pb_size_t>(ostream.bytes_written);
        memcpy(ack.decoded.payload.bytes, payload, ostream.bytes_written);
        packet_queue_.push_back(ack);
        ble_log("Routing ack req_id=%lu resp_id=%lu err=%u",
                static_cast<unsigned long>(request.id),
                static_cast<unsigned long>(ack.id),
                static_cast<unsigned>(reason));
        if (state_ == State::SendPackets)
        {
            service_.notifyFromNum(0);
        }
        return true;
    }

    bool sendAdminResponse(const meshtastic_AdminMessage& resp, const meshtastic_MeshPacket& request)
    {
        uint8_t payload[256];
        pb_ostream_t ostream = pb_ostream_from_buffer(payload, sizeof(payload));
        if (!pb_encode(&ostream, meshtastic_AdminMessage_fields, &resp))
        {
            return false;
        }

        meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
        packet.from = ctx_.getSelfNodeId();
        packet.to = request.from;
        packet.channel = request.channel;
        // Response packet must use a fresh packet id; request correlation belongs in decoded.request_id.
        packet.id = normalizePacketId(0);
        packet.rx_time = nowSeconds();
        packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        zeroInit(packet.decoded);
        packet.decoded.portnum = meshtastic_PortNum_ADMIN_APP;
        packet.decoded.dest = packet.to;
        packet.decoded.source = packet.from;
        packet.decoded.request_id = request.id;
        packet.decoded.want_response = false;
        packet.decoded.payload.size = static_cast<pb_size_t>(ostream.bytes_written);
        memcpy(packet.decoded.payload.bytes, payload, ostream.bytes_written);
        packet_queue_.push_back(packet);
        ble_log("Admin response tag=%d req_id=%lu resp_id=%lu",
                static_cast<int>(resp.which_payload_variant),
                static_cast<unsigned long>(request.id),
                static_cast<unsigned long>(packet.id));
        if (state_ == State::SendPackets)
        {
            service_.notifyFromNum(0);
        }
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
    ble_config_.mode = meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN;
    ble_config_.fixed_pin = 0;

    Preferences prefs;
    if (prefs.begin(kBlePrefsNs, true))
    {
        ble_config_.enabled = prefs.getBool(kBleEnabledKey, true);
        if (prefs.isKey(kBleModeKey))
        {
            uint8_t mode = prefs.getUChar(kBleModeKey,
                                          static_cast<uint8_t>(meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN));
            if (mode <= static_cast<uint8_t>(meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN))
            {
                ble_config_.mode = static_cast<meshtastic_Config_BluetoothConfig_PairingMode>(mode);
            }
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

        module_config_.telemetry.device_update_interval = 3600;
        module_config_.telemetry.device_telemetry_enabled = true;
        module_config_.telemetry.environment_update_interval = 0;
        module_config_.telemetry.environment_measurement_enabled = false;
        module_config_.telemetry.power_update_interval = 0;
        module_config_.telemetry.health_update_interval = 0;
        module_config_.telemetry.air_quality_interval = 0;

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
    ble_log("security enabled=%u mode=%s fixed_pin=%06lu",
            ble_config_.enabled ? 1U : 0U,
            pairingModeName(ble_config_.mode),
            static_cast<unsigned long>(ble_config_.fixed_pin));
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
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
    battery_service_ = nullptr;
    server_ = nullptr;
    to_radio_ = nullptr;
    from_radio_ = nullptr;
    from_radio_sync_ = nullptr;
    from_num_ = nullptr;
    log_radio_ = nullptr;
    battery_level_ = nullptr;
    last_battery_level_ = -2;
    ::ui::BlePairingPopup::hide();
    pending_passkey_.store(0);
    connected_ = false;
    conn_handle_valid_ = false;
    conn_handle_ = 0;
    read_waiting_.store(false);
    from_num_subscribed_ = false;
    from_radio_sync_subscribed_ = false;
    from_radio_sync_sub_value_ = 0;
    last_to_radio_len_ = 0;
    memset(last_to_radio_.data(), 0, last_to_radio_.size());
    phone_api_.reset();
}

void MeshtasticBleService::refreshBatteryLevel(bool notify)
{
    if (!battery_level_)
    {
        return;
    }

    int level = -1;
    if (BoardBase* board = ctx_.getBoard())
    {
        level = board->getBatteryLevel();
    }
    if (level < 0)
    {
        level = 0;
    }
    if (level > 100)
    {
        level = 100;
    }
    if (last_battery_level_ == level)
    {
        return;
    }

    const uint8_t value = static_cast<uint8_t>(level);
    battery_level_->setValue(&value, 1);
    if (notify && connected_)
    {
        battery_level_->notify(&value, 1, BLE_HS_CONN_HANDLE_NONE);
    }
    last_battery_level_ = level;
}

void MeshtasticBleService::update()
{
    const bool show_pin = (ble_config_.mode != meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN);
    const uint32_t passkey = show_pin ? pending_passkey_.load() : 0;
    ::ui::BlePairingPopup::update(
        passkey,
        ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN,
        device_name_.c_str());
    refreshBatteryLevel(true);
    syncMqttProxySettings();
    handleFromPhone();
    pumpMqttProxyMessages();
    handleToPhone();
}

void MeshtasticBleService::onIncomingText(const chat::MeshIncomingText& msg)
{
    if (phone_api_)
    {
        phone_api_->onIncomingText(msg);
        if (phone_api_->isSendingPackets())
        {
            // Match Meshtastic BLE flow: in steady state, notify phone when new data becomes available.
            notifyFromNum(0);
        }
    }
}

void MeshtasticBleService::onIncomingData(const chat::MeshIncomingData& msg)
{
    if (phone_api_)
    {
        phone_api_->onIncomingData(msg);
        if (phone_api_->isSendingPackets())
        {
            // Match Meshtastic BLE flow: in steady state, notify phone when new data becomes available.
            notifyFromNum(0);
        }
    }
}

void MeshtasticBleService::setupService()
{
    service_ = server_->createService(MESH_SERVICE_UUID);
    const bool no_pin = (ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN);
    if (no_pin)
    {
        to_radio_ = service_->createCharacteristic(TORADIO_UUID, NIMBLE_PROPERTY::WRITE);
        from_radio_ = service_->createCharacteristic(FROMRADIO_UUID, NIMBLE_PROPERTY::READ);
        from_num_ = service_->createCharacteristic(FROMNUM_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
        log_radio_ =
            service_->createCharacteristic(LOGRADIO_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, 512U);
    }
    else
    {
        to_radio_ = service_->createCharacteristic(
            TORADIO_UUID,
            NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_AUTHEN | NIMBLE_PROPERTY::WRITE_ENC);
        from_radio_ = service_->createCharacteristic(
            FROMRADIO_UUID,
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::READ_ENC);
        from_num_ = service_->createCharacteristic(
            FROMNUM_UUID,
            NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_AUTHEN |
                NIMBLE_PROPERTY::READ_ENC);
        log_radio_ = service_->createCharacteristic(
            LOGRADIO_UUID,
            NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_AUTHEN |
                NIMBLE_PROPERTY::READ_ENC,
            512U);
    }
    if (kEnableFromRadioSync)
    {
        from_radio_sync_ =
            service_->createCharacteristic(FROMRADIOSYNC_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE);
    }
    else
    {
        from_radio_sync_ = nullptr;
    }

    to_radio_->setCallbacks(new MeshtasticToRadioCallbacks(*this));
    from_radio_->setCallbacks(new MeshtasticFromRadioCallbacks(*this));
    from_num_->setCallbacks(new MeshtasticNotifyStateCallbacks(*this));
    if (from_radio_sync_)
    {
        from_radio_sync_->setCallbacks(new MeshtasticNotifyStateCallbacks(*this));
    }

    service_->start();

    battery_service_ = server_->createService(NimBLEUUID((uint16_t)0x180F));
    battery_level_ = battery_service_->createCharacteristic(
        (uint16_t)0x2A19,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY,
        1);
    if (auto* desc = battery_level_->create2904())
    {
        desc->setFormat(NimBLE2904::FORMAT_UINT8);
        desc->setNamespace(1);
        desc->setUnit(0x27AD);
    }
    battery_service_->start();
    refreshBatteryLevel(false);
}

void MeshtasticBleService::startAdvertising()
{
    if (!server_)
    {
        return;
    }
    NimBLEAdvertising* adv = server_->getAdvertising();
    adv->reset();
    const bool mesh_service_ok = adv->addServiceUUID(MESH_SERVICE_UUID);
    const bool battery_service_ok = adv->addServiceUUID(NimBLEUUID((uint16_t)0x180F));
    adv->enableScanResponse(true);
    adv->setMinInterval(500);
    adv->setMaxInterval(1000);
    const bool name_ok = adv->setName(device_name_);
    const bool start_ok = adv->start();
    ble_log("advertising uuid=%s batt=%u name=%s mesh_ok=%u name_ok=%u start_ok=%u",
            MESH_SERVICE_UUID,
            battery_service_ok ? 1U : 0U,
            device_name_.c_str(),
            mesh_service_ok ? 1U : 0U,
            name_ok ? 1U : 0U,
            start_ok ? 1U : 0U);
}

void MeshtasticBleService::requestHighThroughputConnection()
{
    if (!server_ || !conn_handle_valid_)
    {
        return;
    }
    server_->updateConnParams(conn_handle_, 6, 12, 0, 600);
    ble_log("request conn high-throughput handle=%u", static_cast<unsigned>(conn_handle_));
}

void MeshtasticBleService::requestLowerPowerConnection()
{
    if (!server_ || !conn_handle_valid_)
    {
        return;
    }
    server_->updateConnParams(conn_handle_, 24, 40, 2, 600);
    ble_log("request conn low-power handle=%u", static_cast<unsigned>(conn_handle_));
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

void MeshtasticBleService::syncMqttProxySettings()
{
    auto* mt = getMeshtasticBackend(ctx_);
    if (!mt)
    {
        return;
    }

    const auto& cfg = ctx_.getConfig();
    chat::meshtastic::MtAdapter::MqttProxySettings settings;
    settings.enabled = module_config_.has_mqtt && module_config_.mqtt.enabled;
    settings.proxy_to_client_enabled = module_config_.has_mqtt && module_config_.mqtt.proxy_to_client_enabled;
    settings.encryption_enabled = module_config_.has_mqtt ? module_config_.mqtt.encryption_enabled : true;
    settings.primary_uplink_enabled = cfg.primary_uplink_enabled;
    settings.primary_downlink_enabled = cfg.primary_downlink_enabled;
    settings.secondary_uplink_enabled = cfg.secondary_uplink_enabled;
    settings.secondary_downlink_enabled = cfg.secondary_downlink_enabled;
    settings.root = module_config_.mqtt.root[0] ? module_config_.mqtt.root : kDefaultMqttRoot;
    settings.primary_channel_id = channelDisplayName(ctx_, 0);
    settings.secondary_channel_id = channelDisplayName(ctx_, 1);
    mt->setMqttProxySettings(settings);
}

void MeshtasticBleService::pumpMqttProxyMessages()
{
    if (!phone_api_)
    {
        return;
    }
    auto* mt = getMeshtasticBackend(ctx_);
    if (!mt)
    {
        return;
    }

    meshtastic_MqttClientProxyMessage msg = meshtastic_MqttClientProxyMessage_init_zero;
    bool queued_any = false;
    while (mt->pollMqttProxyMessage(&msg))
    {
        phone_api_->onMqttProxyMessage(msg);
        queued_any = true;
    }
    if (queued_any && phone_api_->isSendingPackets())
    {
        notifyFromNum(0);
    }
}

void MeshtasticBleService::handleToPhone()
{
    if (!phone_api_ || !to_phone_mutex_)
    {
        return;
    }
    const bool sync_active = connected_ && from_radio_sync_ && from_radio_sync_subscribed_;
    const bool waiting_for_read = read_waiting_.load();
    const bool in_send_packets = phone_api_->isSendingPackets();
    const bool can_preload = connected_ && !in_send_packets;

    Frame frame;
    uint32_t from_num = 0;

    if (pending_to_phone_valid_)
    {
        frame = pending_to_phone_;
        from_num = pending_to_phone_from_num_;
    }
    else
    {
        if (!waiting_for_read && !can_preload)
        {
            return;
        }
        if (!sync_active && to_phone_len_ >= kToPhoneQueueDepth)
        {
            return;
        }
        if (!phone_api_->popToPhone(frame, from_num))
        {
            if (waiting_for_read && in_send_packets)
            {
                // In STATE_SEND_PACKETS, a 0-byte FromRadio read is valid and is
                // preferable to stalling the client until a long timeout expires.
                read_waiting_.store(false);
            }
            return;
        }
    }

    if (sync_active)
    {
        bool sent = false;
        // subValue bit 1 => notify, bit 2 => indicate.
        if ((from_radio_sync_sub_value_ & 0x0002U) != 0U)
        {
            sent = from_radio_sync_->indicate(frame.buf.data(), frame.len);
        }
        if (!sent)
        {
            sent = from_radio_sync_->notify(frame.buf.data(), frame.len);
        }
        if (sent)
        {
            pending_to_phone_valid_ = false;
            ble_log("fromRadioSync tx from_num=%lu len=%u",
                    static_cast<unsigned long>(from_num),
                    static_cast<unsigned>(frame.len));
            return;
        }

        // If the app is using FromRadioSync it will not drain legacy FromRadio reads.
        // Keep the frame pending and retry sync on the next update tick.
        pending_to_phone_ = frame;
        pending_to_phone_from_num_ = from_num;
        pending_to_phone_valid_ = true;
        ble_log("fromRadioSync tx busy; defer from_num=%lu len=%u",
                static_cast<unsigned long>(from_num),
                static_cast<unsigned>(frame.len));
        return;
    }

    bool queued = false;
    if (xSemaphoreTake(to_phone_mutex_, pdMS_TO_TICKS(5)) == pdTRUE)
    {
        if (to_phone_len_ < kToPhoneQueueDepth)
        {
            to_phone_queue_[to_phone_len_++] = frame;
            queued = true;
        }
        xSemaphoreGive(to_phone_mutex_);
    }

    if (queued)
    {
        pending_to_phone_valid_ = false;
        ble_log("toPhone enqueue from_num=%lu len=%u q=%u",
                static_cast<unsigned long>(from_num),
                static_cast<unsigned>(frame.len),
                static_cast<unsigned>(to_phone_len_));
        if (in_send_packets && !waiting_for_read)
        {
            notifyFromNum(from_num);
        }
        else if (can_preload && !sync_active && to_phone_len_ < kToPhoneQueueDepth)
        {
            // Match official Meshtastic BLE behavior during config/setup:
            // keep preloading until the queue is full so client reads don't see
            // transient empty responses between config packets.
            handleToPhone();
        }
    }
    else
    {
        pending_to_phone_ = frame;
        pending_to_phone_from_num_ = from_num;
        pending_to_phone_valid_ = true;
        ble_log("toPhone enqueue defer from_num=%lu len=%u (queue full/busy)",
                static_cast<unsigned long>(from_num),
                static_cast<unsigned>(frame.len));
    }
}

bool MeshtasticBleService::shouldBlockOnRead() const
{
    if (!connected_ || !phone_api_)
    {
        return false;
    }
    // Match official Meshtastic BLE behavior:
    // - during steady-state STATE_SEND_PACKETS, the client reads after FROMNUM notify,
    //   so onRead should wait for the main loop to produce the next FromRadio frame
    // - during config/setup, iOS drains FROMRADIO until it sees a 0-byte read; if we
    //   return empty before CONFIG_COMPLETE the app can stop draining and appear stuck
    //   in a "not connected" state. Therefore config flow must also block briefly.
    return phone_api_->isSendingPackets() || phone_api_->isConfigFlowActive();
}

namespace
{
chat::meshtastic::MtAdapter* getMeshtasticBackend(app::AppContext& ctx)
{
    auto* adapter = ctx.getMeshAdapter();
    auto* router = static_cast<chat::MeshAdapterRouter*>(adapter);
    if (!router || router->backendProtocol() != chat::MeshProtocol::Meshtastic)
    {
        return nullptr;
    }
    auto* backend = router->backendForProtocol(router->backendProtocol());
    return static_cast<chat::meshtastic::MtAdapter*>(backend);
}

std::string channelDisplayName(app::AppContext& ctx, uint8_t idx)
{
    const auto& cfg = ctx.getConfig();
    if (idx == 1)
    {
        return "Secondary";
    }
    auto preset = static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(cfg.meshtastic_config.modem_preset);
    return cfg.meshtastic_config.use_preset
               ? std::string(chat::meshtastic::presetDisplayName(preset))
               : std::string("Custom");
}
} // namespace

void MeshtasticBleService::notifyFromNum(uint32_t value)
{
    if (!from_num_)
    {
        return;
    }
    if (!from_num_subscribed_)
    {
        ble_log("fromNum skip notify value=%lu (not subscribed)", static_cast<unsigned long>(value));
        return;
    }
    const uint32_t notify_value = value;
    uint8_t val[4] = {
        static_cast<uint8_t>(notify_value & 0xFFU),
        static_cast<uint8_t>((notify_value >> 8) & 0xFFU),
        static_cast<uint8_t>((notify_value >> 16) & 0xFFU),
        static_cast<uint8_t>((notify_value >> 24) & 0xFFU),
    };
    from_num_->setValue(val, sizeof(val));
    bool has_connection = connected_;
    if (server_)
    {
        has_connection = (server_->getConnectedCount() > 0);
    }
    if (has_connection)
    {
        ble_log("fromNum notify value=%lu", static_cast<unsigned long>(notify_value));
        from_num_->notify();
    }
    else
    {
        ble_log("fromNum skip notify value=%lu (no connection)", static_cast<unsigned long>(notify_value));
    }
}

void MeshtasticBleService::clearQueues()
{
    read_waiting_.store(false);
    pending_to_phone_valid_ = false;
    pending_to_phone_from_num_ = 0;

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

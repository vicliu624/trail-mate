#include "chat/ble/meshtastic_phone_core.h"

#include "chat/ble/meshtastic_defaults.h"
#include "chat/ble/meshtastic_phone_config_bridge.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/runtime/self_identity_policy.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "sys/clock.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace ble
{
namespace
{

constexpr uint8_t kQueueDepthHint = 4;
constexpr meshtastic_AdminMessage_ConfigType kConfigSnapshotTypes[] = {
    meshtastic_AdminMessage_ConfigType_DEVICE_CONFIG,
    meshtastic_AdminMessage_ConfigType_POSITION_CONFIG,
    meshtastic_AdminMessage_ConfigType_DISPLAY_CONFIG,
    meshtastic_AdminMessage_ConfigType_LORA_CONFIG,
    meshtastic_AdminMessage_ConfigType_BLUETOOTH_CONFIG,
    meshtastic_AdminMessage_ConfigType_SECURITY_CONFIG,
    meshtastic_AdminMessage_ConfigType_DEVICEUI_CONFIG,
};
constexpr meshtastic_AdminMessage_ModuleConfigType kModuleSnapshotTypes[] = {
    meshtastic_AdminMessage_ModuleConfigType_MQTT_CONFIG,
    meshtastic_AdminMessage_ModuleConfigType_SERIAL_CONFIG,
    meshtastic_AdminMessage_ModuleConfigType_EXTNOTIF_CONFIG,
    meshtastic_AdminMessage_ModuleConfigType_STOREFORWARD_CONFIG,
    meshtastic_AdminMessage_ModuleConfigType_RANGETEST_CONFIG,
    meshtastic_AdminMessage_ModuleConfigType_TELEMETRY_CONFIG,
    meshtastic_AdminMessage_ModuleConfigType_CANNEDMSG_CONFIG,
    meshtastic_AdminMessage_ModuleConfigType_AUDIO_CONFIG,
    meshtastic_AdminMessage_ModuleConfigType_REMOTEHARDWARE_CONFIG,
    meshtastic_AdminMessage_ModuleConfigType_NEIGHBORINFO_CONFIG,
    meshtastic_AdminMessage_ModuleConfigType_AMBIENTLIGHTING_CONFIG,
    meshtastic_AdminMessage_ModuleConfigType_DETECTIONSENSOR_CONFIG,
    meshtastic_AdminMessage_ModuleConfigType_PAXCOUNTER_CONFIG,
};

void logDual(const char* format, ...)
{
    if (!format)
    {
        return;
    }

    char buffer[192] = {};
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    std::printf("%s", buffer);
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
    std::strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

bool parsePosixTzOffsetMinutes(const char* tzdef, int* out_offset_min)
{
    if (!tzdef || !out_offset_min || tzdef[0] == '\0')
    {
        return false;
    }

    const char* cursor = tzdef;
    if (*cursor == '<')
    {
        ++cursor;
        while (*cursor != '\0' && *cursor != '>')
        {
            ++cursor;
        }
        if (*cursor != '>')
        {
            return false;
        }
        ++cursor;
    }
    else
    {
        size_t name_len = 0;
        while (*cursor != '\0' && std::isalpha(static_cast<unsigned char>(*cursor)))
        {
            ++cursor;
            ++name_len;
        }
        if (name_len < 3U)
        {
            return false;
        }
    }

    int sign = 1;
    if (*cursor == '-')
    {
        sign = -1;
        ++cursor;
    }
    else if (*cursor == '+')
    {
        ++cursor;
    }

    if (!std::isdigit(static_cast<unsigned char>(*cursor)))
    {
        return false;
    }

    int hours = 0;
    while (std::isdigit(static_cast<unsigned char>(*cursor)))
    {
        hours = (hours * 10) + (*cursor - '0');
        ++cursor;
    }

    int minutes = 0;
    if (*cursor == ':')
    {
        ++cursor;
        if (!std::isdigit(static_cast<unsigned char>(*cursor)))
        {
            return false;
        }
        while (std::isdigit(static_cast<unsigned char>(*cursor)))
        {
            minutes = (minutes * 10) + (*cursor - '0');
            ++cursor;
        }

        if (*cursor == ':')
        {
            ++cursor;
            while (std::isdigit(static_cast<unsigned char>(*cursor)))
            {
                ++cursor;
            }
        }
    }

    const int posix_offset_min = sign * ((hours * 60) + minutes);
    *out_offset_min = -posix_offset_min;
    return true;
}

void buildFixedPosixTzdef(int offset_min, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }

    const int posix_offset_min = -offset_min;
    const int abs_minutes = posix_offset_min < 0 ? -posix_offset_min : posix_offset_min;
    const int abs_hours = abs_minutes / 60;
    const int rem_minutes = abs_minutes % 60;

    if (rem_minutes == 0)
    {
        std::snprintf(out, out_len, "UTC%+d", posix_offset_min / 60);
    }
    else
    {
        std::snprintf(out,
                      out_len,
                      "UTC%+d:%02d",
                      posix_offset_min < 0 ? -abs_hours : abs_hours,
                      rem_minutes);
    }
}

bool loadStoredTimezoneTzdef(const MeshtasticPhoneDeviceRuntimeHooks* device_runtime_hooks, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return false;
    }

    out[0] = '\0';
    if (!device_runtime_hooks)
    {
        return false;
    }
    return device_runtime_hooks->loadTimezoneTzdef(out, out_len);
}

void saveStoredTimezoneTzdef(MeshtasticPhoneDeviceRuntimeHooks* device_runtime_hooks, const char* tzdef)
{
    if (!device_runtime_hooks)
    {
        return;
    }
    device_runtime_hooks->saveTimezoneTzdef(tzdef);
}

void buildLinkedTimezoneTzdef(const MeshtasticPhoneDeviceRuntimeHooks* device_runtime_hooks, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }

    out[0] = '\0';
    const int current_offset_min = device_runtime_hooks ? device_runtime_hooks->getTimezoneOffsetMinutes() : 0;
    char stored[65] = {};
    if (loadStoredTimezoneTzdef(device_runtime_hooks, stored, sizeof(stored)))
    {
        int parsed_offset_min = 0;
        if (parsePosixTzOffsetMinutes(stored, &parsed_offset_min) && parsed_offset_min == current_offset_min)
        {
            copyBounded(out, out_len, stored);
            return;
        }
    }

    buildFixedPosixTzdef(current_offset_min, out, out_len);
}

uint32_t nowSeconds()
{
    return sys::epoch_seconds_now();
}

bool buildSelfPositionPayload(const MeshtasticPhoneDeviceRuntimeHooks* device_runtime_hooks,
                              uint8_t* out_buf,
                              size_t* out_len)
{
    if (!out_buf || !out_len || *out_len == 0)
    {
        return false;
    }

    MeshtasticGpsFix gps_fix{};
    if (!device_runtime_hooks || !device_runtime_hooks->getGpsFix(&gps_fix) || !gps_fix.valid)
    {
        return false;
    }

    meshtastic_Position pos = meshtastic_Position_init_zero;
    pos.has_latitude_i = true;
    pos.latitude_i = static_cast<int32_t>(std::lround(gps_fix.lat * 1e7));
    pos.has_longitude_i = true;
    pos.longitude_i = static_cast<int32_t>(std::lround(gps_fix.lng * 1e7));
    pos.location_source = meshtastic_Position_LocSource_LOC_INTERNAL;

    if (gps_fix.has_alt)
    {
        pos.has_altitude = true;
        pos.altitude = static_cast<int32_t>(std::lround(gps_fix.alt_m));
        pos.altitude_source = meshtastic_Position_AltSource_ALT_INTERNAL;
    }
    if (gps_fix.has_speed)
    {
        pos.has_ground_speed = true;
        pos.ground_speed = static_cast<uint32_t>(std::lround(gps_fix.speed_mps));
    }
    if (gps_fix.has_course)
    {
        double course = gps_fix.course_deg;
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
    if (gps_fix.satellites > 0)
    {
        pos.sats_in_view = gps_fix.satellites;
    }

    const uint32_t ts = nowSeconds();
    if (ts >= 1577836800U)
    {
        pos.timestamp = ts;
    }

    pb_ostream_t stream = pb_ostream_from_buffer(out_buf, *out_len);
    if (!pb_encode(&stream, meshtastic_Position_fields, &pos))
    {
        return false;
    }

    *out_len = stream.bytes_written;
    return true;
}

uint8_t channelIndexFromId(chat::ChannelId channel)
{
    return (channel == chat::ChannelId::SECONDARY) ? 1U : 0U;
}

void logRuntimeFootprint(const char* stage)
{
    logDual("[BLE][mtcore][rt] stage=%s\n", stage ? stage : "unknown");
}

const char* configStageName(uint32_t nonce)
{
    switch (nonce)
    {
    case meshtastic_defaults::kConfigNonceOnlyConfig:
        return "stage1_config";
    case meshtastic_defaults::kConfigNonceOnlyNodes:
        return "stage2_nodes";
    default:
        return "stage_unknown";
    }
}

meshtastic_Config_DeviceConfig_Role roleFromEntry(uint8_t role)
{
    switch (role)
    {
    case 0:
        return meshtastic_Config_DeviceConfig_Role_CLIENT;
    case 1:
        return meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE;
    case 2:
        return meshtastic_Config_DeviceConfig_Role_ROUTER;
    case 3:
        return meshtastic_Config_DeviceConfig_Role_ROUTER_CLIENT;
    case 4:
        return meshtastic_Config_DeviceConfig_Role_REPEATER;
    case 5:
        return meshtastic_Config_DeviceConfig_Role_TRACKER;
    case 6:
        return meshtastic_Config_DeviceConfig_Role_SENSOR;
    case 7:
        return meshtastic_Config_DeviceConfig_Role_TAK;
    case 8:
        return meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN;
    case 9:
        return meshtastic_Config_DeviceConfig_Role_LOST_AND_FOUND;
    case 10:
        return meshtastic_Config_DeviceConfig_Role_TAK_TRACKER;
    case 11:
        return meshtastic_Config_DeviceConfig_Role_ROUTER_LATE;
    case 12:
        return meshtastic_Config_DeviceConfig_Role_CLIENT_BASE;
    default:
        return meshtastic_Config_DeviceConfig_Role_CLIENT;
    }
}

void initDefaultModuleConfig(meshtastic_LocalModuleConfig* out, uint32_t self_node)
{
    meshtastic_config_bridge::initDefaultModuleConfig(out, self_node);
    if (out)
    {
        out->ambient_lighting.current = 8;
    }
}

void applyLegacyMqttDefaults(meshtastic_LocalModuleConfig* out)
{
    meshtastic_config_bridge::normalizeModuleConfig(out);
}

bool moduleConfigTypeFromVariant(pb_size_t variant_tag, meshtastic_AdminMessage_ModuleConfigType* out)
{
    if (!out)
    {
        return false;
    }
    switch (variant_tag)
    {
    case meshtastic_ModuleConfig_mqtt_tag:
        *out = meshtastic_AdminMessage_ModuleConfigType_MQTT_CONFIG;
        return true;
    case meshtastic_ModuleConfig_serial_tag:
        *out = meshtastic_AdminMessage_ModuleConfigType_SERIAL_CONFIG;
        return true;
    case meshtastic_ModuleConfig_external_notification_tag:
        *out = meshtastic_AdminMessage_ModuleConfigType_EXTNOTIF_CONFIG;
        return true;
    case meshtastic_ModuleConfig_store_forward_tag:
        *out = meshtastic_AdminMessage_ModuleConfigType_STOREFORWARD_CONFIG;
        return true;
    case meshtastic_ModuleConfig_range_test_tag:
        *out = meshtastic_AdminMessage_ModuleConfigType_RANGETEST_CONFIG;
        return true;
    case meshtastic_ModuleConfig_telemetry_tag:
        *out = meshtastic_AdminMessage_ModuleConfigType_TELEMETRY_CONFIG;
        return true;
    case meshtastic_ModuleConfig_canned_message_tag:
        *out = meshtastic_AdminMessage_ModuleConfigType_CANNEDMSG_CONFIG;
        return true;
    case meshtastic_ModuleConfig_audio_tag:
        *out = meshtastic_AdminMessage_ModuleConfigType_AUDIO_CONFIG;
        return true;
    case meshtastic_ModuleConfig_remote_hardware_tag:
        *out = meshtastic_AdminMessage_ModuleConfigType_REMOTEHARDWARE_CONFIG;
        return true;
    case meshtastic_ModuleConfig_neighbor_info_tag:
        *out = meshtastic_AdminMessage_ModuleConfigType_NEIGHBORINFO_CONFIG;
        return true;
    case meshtastic_ModuleConfig_ambient_lighting_tag:
        *out = meshtastic_AdminMessage_ModuleConfigType_AMBIENTLIGHTING_CONFIG;
        return true;
    case meshtastic_ModuleConfig_detection_sensor_tag:
        *out = meshtastic_AdminMessage_ModuleConfigType_DETECTIONSENSOR_CONFIG;
        return true;
    case meshtastic_ModuleConfig_paxcounter_tag:
        *out = meshtastic_AdminMessage_ModuleConfigType_PAXCOUNTER_CONFIG;
        return true;
    default:
        return false;
    }
}

} // namespace

MeshtasticPhoneCore::MeshtasticPhoneCore(IPhoneRuntimeContext& ctx,
                                         MeshtasticPhoneTransport& transport,
                                         MeshtasticPhoneBluetoothConfigHooks* bluetooth_config_hooks,
                                         MeshtasticPhoneModuleConfigHooks* module_config_hooks,
                                         MeshtasticPhoneConfigLifecycleHooks* config_lifecycle_hooks,
                                         MeshtasticPhoneStatusHooks* status_hooks,
                                         MeshtasticPhoneMqttHooks* mqtt_hooks,
                                         MeshtasticPhoneDeviceRuntimeHooks* device_runtime_hooks)
    : ctx_(ctx),
      transport_(transport),
      bluetooth_config_hooks_(bluetooth_config_hooks),
      module_config_hooks_(module_config_hooks),
      config_lifecycle_hooks_(config_lifecycle_hooks),
      status_hooks_(status_hooks),
      mqtt_hooks_(mqtt_hooks),
      device_runtime_hooks_(device_runtime_hooks)
{
    bluetooth_config_ = meshtastic_Config_BluetoothConfig_init_zero;
    bluetooth_config_.enabled = ctx_.isBleEnabled();
    bluetooth_config_.mode = meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN;
    bluetooth_config_.fixed_pin = 0;
    if (bluetooth_config_hooks_)
    {
        meshtastic_Config_BluetoothConfig loaded_bt = meshtastic_Config_BluetoothConfig_init_zero;
        if (bluetooth_config_hooks_->loadBluetoothConfig(&loaded_bt))
        {
            bluetooth_config_ = loaded_bt;
        }
    }
    initDefaultModuleConfig(&module_config_, ctx_.getSelfNodeId());
    if (module_config_hooks_)
    {
        meshtastic_LocalModuleConfig loaded = meshtastic_LocalModuleConfig_init_zero;
        if (module_config_hooks_->loadModuleConfig(&loaded))
        {
            module_config_ = loaded;
            applyLegacyMqttDefaults(&module_config_);
        }
    }
}

void MeshtasticPhoneCore::reset()
{
    config_nonce_ = 0;
    config_node_index_ = 0;
    config_channel_index_ = 0;
    config_type_index_ = 0;
    config_module_type_index_ = 0;
    last_to_radio_len_ = 0;
    std::memset(last_to_radio_, 0, sizeof(last_to_radio_));
    config_flow_active_ = false;
    frame_queue_.clear();
    queue_status_queue_.clear();
    packet_queue_.clear();
}

void MeshtasticPhoneCore::onIncomingText(const chat::MeshIncomingText& msg)
{
    packet_queue_.push_back(buildPacketFromText(msg));
    logDual("[BLE][mtcore] enqueue text packet id=%08lX from=%08lX to=%08lX len=%u\n",
            static_cast<unsigned long>(packet_queue_.back().id),
            static_cast<unsigned long>(packet_queue_.back().from),
            static_cast<unsigned long>(packet_queue_.back().to),
            static_cast<unsigned>(packet_queue_.back().decoded.payload.size));
    notifyFromNum(packet_queue_.back().id);
}

void MeshtasticPhoneCore::onIncomingData(const chat::MeshIncomingData& msg)
{
    packet_queue_.push_back(buildPacketFromData(msg));
    logDual("[BLE][mtcore] enqueue data packet id=%08lX port=%u req=%08lX from=%08lX to=%08lX len=%u\n",
            static_cast<unsigned long>(packet_queue_.back().id),
            static_cast<unsigned>(packet_queue_.back().decoded.portnum),
            static_cast<unsigned long>(packet_queue_.back().decoded.request_id),
            static_cast<unsigned long>(packet_queue_.back().from),
            static_cast<unsigned long>(packet_queue_.back().to),
            static_cast<unsigned>(packet_queue_.back().decoded.payload.size));
    notifyFromNum(packet_queue_.back().id);
}

bool MeshtasticPhoneCore::isSendingPackets() const
{
    return !config_flow_active_;
}

bool MeshtasticPhoneCore::isConfigFlowActive() const
{
    return config_flow_active_;
}

bool MeshtasticPhoneCore::handleToRadio(const uint8_t* data, size_t len)
{
    if (!data || len == 0 || len > sizeof(last_to_radio_))
    {
        return false;
    }

    if (last_to_radio_len_ == len && std::memcmp(last_to_radio_, data, len) == 0)
    {
        return true;
    }
    std::memcpy(last_to_radio_, data, len);
    last_to_radio_len_ = len;

    auto& to_radio = to_radio_scratch_;
    std::memset(&to_radio, 0, sizeof(to_radio));
    pb_istream_t stream = pb_istream_from_buffer(data, len);
    if (!pb_decode(&stream, meshtastic_ToRadio_fields, &to_radio))
    {
        return false;
    }

    logDual("[BLE][mtcore] to_radio variant=%u len=%u\n",
            static_cast<unsigned>(to_radio.which_payload_variant),
            static_cast<unsigned>(len));

    switch (to_radio.which_payload_variant)
    {
    case meshtastic_ToRadio_packet_tag:
        return handleToRadioPacket(to_radio.packet);
    case meshtastic_ToRadio_mqttClientProxyMessage_tag:
        return mqtt_hooks_ ? mqtt_hooks_->handleMqttProxyToRadio(to_radio.mqttClientProxyMessage) : false;
    case meshtastic_ToRadio_want_config_id_tag:
        logDual("[BLE][mtcore][flow] want_config nonce=%08lX stage=%s\n",
                static_cast<unsigned long>(to_radio.want_config_id),
                configStageName(to_radio.want_config_id));
        enqueueConfigSnapshot(to_radio.want_config_id);
        return true;
    case meshtastic_ToRadio_heartbeat_tag:
        enqueueQueueStatus(to_radio.heartbeat.nonce, true);
        return true;
    case meshtastic_ToRadio_disconnect_tag:
        reset();
        return true;
    default:
        return false;
    }
}

bool MeshtasticPhoneCore::handleToRadioPacket(meshtastic_MeshPacket& packet)
{
    if (packet.which_payload_variant != meshtastic_MeshPacket_decoded_tag)
    {
        enqueueQueueStatus(packet.id, false);
        return false;
    }

    if (packet.id == 0)
    {
        packet.id = sys::millis_now();
    }

    packet.from = ctx_.getSelfNodeId();
    packet.rx_time = nowSeconds();
    logDual("[BLE][mtcore] packet port=%u to=%08lX want_resp=%u len=%u\n",
            static_cast<unsigned>(packet.decoded.portnum),
            static_cast<unsigned long>(packet.to),
            packet.decoded.want_response ? 1U : 0U,
            static_cast<unsigned>(packet.decoded.payload.size));

    const bool admin_for_self =
        (packet.decoded.portnum == meshtastic_PortNum_ADMIN_APP) &&
        (packet.to == 0 || packet.to == ctx_.getSelfNodeId());
    if (admin_for_self)
    {
        const bool ok = handleAdmin(packet);
        enqueueQueueStatus(packet.id, ok);
        return ok;
    }

    if (handleLocalSelfPacket(packet))
    {
        enqueueQueueStatus(packet.id, true);
        return true;
    }

    const chat::ChannelId channel = (packet.channel == 1) ? chat::ChannelId::SECONDARY : chat::ChannelId::PRIMARY;

    if (packet.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP)
    {
        std::string text(reinterpret_cast<const char*>(packet.decoded.payload.bytes), packet.decoded.payload.size);
        const chat::MessageId msg_id = ctx_.getChatService().sendTextWithId(channel, text, packet.id, packet.to);
        enqueueQueueStatus(packet.id, msg_id != 0);
        return msg_id != 0;
    }

    chat::IMeshAdapter* adapter = ctx_.getMeshAdapter();
    if (!adapter)
    {
        enqueueQueueStatus(packet.id, false);
        return false;
    }

    const bool ok = adapter->sendAppData(channel,
                                         static_cast<uint32_t>(packet.decoded.portnum),
                                         packet.decoded.payload.bytes,
                                         packet.decoded.payload.size,
                                         packet.to,
                                         packet.want_ack,
                                         packet.id,
                                         packet.decoded.want_response);
    enqueueQueueStatus(packet.id, ok);
    return ok;
}

bool MeshtasticPhoneCore::handleAdmin(meshtastic_MeshPacket& packet)
{
    std::memset(&admin_req_scratch_, 0, sizeof(admin_req_scratch_));
    pb_istream_t stream = pb_istream_from_buffer(packet.decoded.payload.bytes, packet.decoded.payload.size);
    if (!pb_decode(&stream, meshtastic_AdminMessage_fields, &admin_req_scratch_))
    {
        return false;
    }

    std::memset(&admin_resp_scratch_, 0, sizeof(admin_resp_scratch_));
    meshtastic_AdminMessage& req = admin_req_scratch_;
    meshtastic_AdminMessage& resp = admin_resp_scratch_;
    bool has_resp = false;
    auto cfg = ctx_.getMeshtasticPhoneConfig();

    switch (req.which_payload_variant)
    {
    case meshtastic_AdminMessage_get_owner_request_tag:
        resp.which_payload_variant = meshtastic_AdminMessage_get_owner_response_tag;
        resp.get_owner_response = buildSelfNodeInfo().user;
        has_resp = true;
        break;
    case meshtastic_AdminMessage_get_channel_request_tag:
        resp.which_payload_variant = meshtastic_AdminMessage_get_channel_response_tag;
        resp.get_channel_response = buildChannel(static_cast<uint8_t>(req.get_channel_request > 0 ? (req.get_channel_request - 1) : 0));
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
        resp.which_payload_variant = meshtastic_AdminMessage_get_device_connection_status_response_tag;
        {
            meshtastic_DeviceConnectionStatus status = meshtastic_DeviceConnectionStatus_init_zero;
            resp.get_device_connection_status_response = status;
        }
        if (!(status_hooks_ && status_hooks_->loadDeviceConnectionStatus(&resp.get_device_connection_status_response)))
        {
            resp.get_device_connection_status_response.has_bluetooth = true;
            resp.get_device_connection_status_response.bluetooth.pin = 0;
            resp.get_device_connection_status_response.bluetooth.rssi = 0;
            resp.get_device_connection_status_response.bluetooth.is_connected = transport_.isBleConnected();
        }
        has_resp = true;
        break;
    case meshtastic_AdminMessage_get_ui_config_request_tag:
        resp.which_payload_variant = meshtastic_AdminMessage_get_ui_config_response_tag;
        resp.get_ui_config_response = buildDeviceUi();
        has_resp = true;
        break;
    case meshtastic_AdminMessage_set_owner_tag:
        copyBounded(cfg.node_name, sizeof(cfg.node_name), req.set_owner.long_name);
        copyBounded(cfg.short_name, sizeof(cfg.short_name), req.set_owner.short_name);
        ctx_.setMeshtasticPhoneConfig(cfg);
        ctx_.saveConfig();
        ctx_.applyUserInfo();
        resp.which_payload_variant = meshtastic_AdminMessage_get_owner_response_tag;
        resp.get_owner_response = buildSelfNodeInfo().user;
        has_resp = true;
        break;
    case meshtastic_AdminMessage_set_channel_tag:
        if (req.set_channel.index == 0)
        {
            cfg.primary_enabled = (req.set_channel.role != meshtastic_Channel_Role_DISABLED);
            cfg.primary_uplink_enabled = req.set_channel.settings.uplink_enabled;
            cfg.primary_downlink_enabled = req.set_channel.settings.downlink_enabled;
            if (req.set_channel.settings.psk.size == 16)
            {
                std::memcpy(cfg.mesh.primary_key, req.set_channel.settings.psk.bytes, 16);
            }
            else if (req.set_channel.settings.psk.size == 0)
            {
                std::memset(cfg.mesh.primary_key, 0, sizeof(cfg.mesh.primary_key));
            }
        }
        else if (req.set_channel.index == 1)
        {
            cfg.secondary_enabled = (req.set_channel.role != meshtastic_Channel_Role_DISABLED);
            cfg.secondary_uplink_enabled = req.set_channel.settings.uplink_enabled;
            cfg.secondary_downlink_enabled = req.set_channel.settings.downlink_enabled;
            if (req.set_channel.settings.psk.size == 16)
            {
                std::memcpy(cfg.mesh.secondary_key, req.set_channel.settings.psk.bytes, 16);
            }
            else if (req.set_channel.settings.psk.size == 0)
            {
                std::memset(cfg.mesh.secondary_key, 0, sizeof(cfg.mesh.secondary_key));
            }
        }
        ctx_.setMeshtasticPhoneConfig(cfg);
        ctx_.saveConfig();
        ctx_.applyMeshConfig();
        resp.which_payload_variant = meshtastic_AdminMessage_get_channel_response_tag;
        resp.get_channel_response = buildChannel(req.set_channel.index);
        has_resp = true;
        break;
    case meshtastic_AdminMessage_set_config_tag:
        switch (req.set_config.which_payload_variant)
        {
        case meshtastic_Config_lora_tag:
            logDual("[BLE][mtcore] set_config lora start ok_to_mqtt=%u ignore_mqtt=%u hop=%u ch=%u preset=%u\n",
                    req.set_config.payload_variant.lora.config_ok_to_mqtt ? 1U : 0U,
                    req.set_config.payload_variant.lora.ignore_mqtt ? 1U : 0U,
                    static_cast<unsigned>(req.set_config.payload_variant.lora.hop_limit),
                    static_cast<unsigned>(req.set_config.payload_variant.lora.channel_num),
                    static_cast<unsigned>(req.set_config.payload_variant.lora.modem_preset));
            cfg.mesh.use_preset = req.set_config.payload_variant.lora.use_preset;
            cfg.mesh.modem_preset = static_cast<uint8_t>(req.set_config.payload_variant.lora.modem_preset);
            cfg.mesh.bandwidth_khz = req.set_config.payload_variant.lora.bandwidth;
            cfg.mesh.spread_factor = static_cast<uint8_t>(req.set_config.payload_variant.lora.spread_factor);
            cfg.mesh.coding_rate = req.set_config.payload_variant.lora.coding_rate;
            cfg.mesh.frequency_offset_mhz = req.set_config.payload_variant.lora.frequency_offset;
            cfg.mesh.region = static_cast<uint8_t>(req.set_config.payload_variant.lora.region);
            cfg.mesh.hop_limit = static_cast<uint8_t>(req.set_config.payload_variant.lora.hop_limit);
            cfg.mesh.tx_enabled = req.set_config.payload_variant.lora.tx_enabled;
            cfg.mesh.tx_power = req.set_config.payload_variant.lora.tx_power;
            cfg.mesh.channel_num = req.set_config.payload_variant.lora.channel_num;
            cfg.mesh.override_duty_cycle = req.set_config.payload_variant.lora.override_duty_cycle;
            cfg.mesh.override_frequency_mhz = req.set_config.payload_variant.lora.override_frequency;
            cfg.mesh.ignore_mqtt = req.set_config.payload_variant.lora.ignore_mqtt;
            cfg.mesh.config_ok_to_mqtt = req.set_config.payload_variant.lora.config_ok_to_mqtt;
            logDual("[BLE][mtcore] set_config lora pre-save ok_to_mqtt=%u ignore_mqtt=%u\n",
                    cfg.mesh.config_ok_to_mqtt ? 1U : 0U,
                    cfg.mesh.ignore_mqtt ? 1U : 0U);
            ctx_.setMeshtasticPhoneConfig(cfg);
            ctx_.saveConfig();
            logDual("[BLE][mtcore] set_config lora post-save\n");
            ctx_.applyMeshConfig();
            logDual("[BLE][mtcore] set_config lora post-apply\n");
            break;
        case meshtastic_Config_position_tag:
            cfg.gps_enabled = req.set_config.payload_variant.position.gps_enabled;
            cfg.gps_interval_ms = req.set_config.payload_variant.position.gps_update_interval * 1000U;
            ctx_.setMeshtasticPhoneConfig(cfg);
            ctx_.saveConfig();
            ctx_.applyPositionConfig();
            break;
        case meshtastic_Config_bluetooth_tag:
            bluetooth_config_ = req.set_config.payload_variant.bluetooth;
            bluetooth_config_.enabled = req.set_config.payload_variant.bluetooth.enabled;
            if (bluetooth_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN)
            {
                bluetooth_config_.fixed_pin = 0;
            }
            if (bluetooth_config_hooks_)
            {
                bluetooth_config_hooks_->saveBluetoothConfig(bluetooth_config_);
            }
            ctx_.setBleEnabled(req.set_config.payload_variant.bluetooth.enabled);
            break;
        case meshtastic_Config_device_tag:
        {
            const char* tzdef = req.set_config.payload_variant.device.tzdef;
            if (tzdef[0] != '\0')
            {
                int offset_min = 0;
                if (parsePosixTzOffsetMinutes(tzdef, &offset_min))
                {
                    if (device_runtime_hooks_)
                    {
                        device_runtime_hooks_->setTimezoneOffsetMinutes(offset_min);
                    }
                    saveStoredTimezoneTzdef(device_runtime_hooks_, tzdef);
                    logDual("[BLE][mtcore] set_config device tzdef=%s offset_min=%d\n", tzdef, offset_min);
                }
                else
                {
                    logDual("[BLE][mtcore] set_config device tzdef parse failed tzdef=%s\n", tzdef);
                }
            }
            break;
        }
        case meshtastic_Config_device_ui_tag:
            break;
        case meshtastic_Config_display_tag:
            break;
        default:
            break;
        }
        resp.which_payload_variant = meshtastic_AdminMessage_get_config_response_tag;
        if (req.set_config.which_payload_variant == meshtastic_Config_position_tag)
        {
            resp.get_config_response = buildConfig(meshtastic_AdminMessage_ConfigType_POSITION_CONFIG);
        }
        else if (req.set_config.which_payload_variant == meshtastic_Config_bluetooth_tag)
        {
            resp.get_config_response = buildConfig(meshtastic_AdminMessage_ConfigType_BLUETOOTH_CONFIG);
        }
        else if (req.set_config.which_payload_variant == meshtastic_Config_device_tag)
        {
            resp.get_config_response = buildConfig(meshtastic_AdminMessage_ConfigType_DEVICE_CONFIG);
        }
        else if (req.set_config.which_payload_variant == meshtastic_Config_display_tag)
        {
            resp.get_config_response = buildConfig(meshtastic_AdminMessage_ConfigType_DISPLAY_CONFIG);
        }
        else if (req.set_config.which_payload_variant == meshtastic_Config_device_ui_tag)
        {
            resp.get_config_response = buildConfig(meshtastic_AdminMessage_ConfigType_DEVICEUI_CONFIG);
        }
        else
        {
            resp.get_config_response = buildConfig(meshtastic_AdminMessage_ConfigType_LORA_CONFIG);
        }
        has_resp = true;
        break;
    case meshtastic_AdminMessage_set_module_config_tag:
    {
        switch (req.set_module_config.which_payload_variant)
        {
        case meshtastic_ModuleConfig_mqtt_tag:
            logDual("[BLE][mtcore] set_module_config mqtt start enabled=%u proxy=%u enc=%u root=%s\n",
                    req.set_module_config.payload_variant.mqtt.enabled ? 1U : 0U,
                    req.set_module_config.payload_variant.mqtt.proxy_to_client_enabled ? 1U : 0U,
                    req.set_module_config.payload_variant.mqtt.encryption_enabled ? 1U : 0U,
                    req.set_module_config.payload_variant.mqtt.root);
            module_config_.has_mqtt = true;
            module_config_.mqtt = req.set_module_config.payload_variant.mqtt;
            applyLegacyMqttDefaults(&module_config_);
            break;
        case meshtastic_ModuleConfig_serial_tag:
            module_config_.has_serial = true;
            module_config_.serial = req.set_module_config.payload_variant.serial;
            break;
        case meshtastic_ModuleConfig_external_notification_tag:
            module_config_.has_external_notification = true;
            module_config_.external_notification = req.set_module_config.payload_variant.external_notification;
            break;
        case meshtastic_ModuleConfig_store_forward_tag:
            module_config_.has_store_forward = true;
            module_config_.store_forward = req.set_module_config.payload_variant.store_forward;
            break;
        case meshtastic_ModuleConfig_range_test_tag:
            module_config_.has_range_test = true;
            module_config_.range_test = req.set_module_config.payload_variant.range_test;
            break;
        case meshtastic_ModuleConfig_telemetry_tag:
            module_config_.has_telemetry = true;
            module_config_.telemetry = req.set_module_config.payload_variant.telemetry;
            break;
        case meshtastic_ModuleConfig_canned_message_tag:
            module_config_.has_canned_message = true;
            module_config_.canned_message = req.set_module_config.payload_variant.canned_message;
            break;
        case meshtastic_ModuleConfig_audio_tag:
            module_config_.has_audio = true;
            module_config_.audio = req.set_module_config.payload_variant.audio;
            break;
        case meshtastic_ModuleConfig_remote_hardware_tag:
            module_config_.has_remote_hardware = true;
            module_config_.remote_hardware = req.set_module_config.payload_variant.remote_hardware;
            break;
        case meshtastic_ModuleConfig_neighbor_info_tag:
            module_config_.has_neighbor_info = true;
            module_config_.neighbor_info = req.set_module_config.payload_variant.neighbor_info;
            break;
        case meshtastic_ModuleConfig_ambient_lighting_tag:
            module_config_.has_ambient_lighting = true;
            module_config_.ambient_lighting = req.set_module_config.payload_variant.ambient_lighting;
            break;
        case meshtastic_ModuleConfig_detection_sensor_tag:
            module_config_.has_detection_sensor = true;
            module_config_.detection_sensor = req.set_module_config.payload_variant.detection_sensor;
            break;
        case meshtastic_ModuleConfig_paxcounter_tag:
            module_config_.has_paxcounter = true;
            module_config_.paxcounter = req.set_module_config.payload_variant.paxcounter;
            break;
        default:
            break;
        }
        if (module_config_hooks_)
        {
            logDual("[BLE][mtcore] set_module_config pre-save variant=%u\n",
                    static_cast<unsigned>(req.set_module_config.which_payload_variant));
            module_config_hooks_->saveModuleConfig(module_config_);
            logDual("[BLE][mtcore] set_module_config post-save variant=%u\n",
                    static_cast<unsigned>(req.set_module_config.which_payload_variant));
        }

        resp.which_payload_variant = meshtastic_AdminMessage_get_module_config_response_tag;
        meshtastic_AdminMessage_ModuleConfigType module_type = meshtastic_AdminMessage_ModuleConfigType_MQTT_CONFIG;
        if (moduleConfigTypeFromVariant(req.set_module_config.which_payload_variant, &module_type))
        {
            resp.get_module_config_response = buildModuleConfig(module_type);
        }
        else
        {
            resp.get_module_config_response = req.set_module_config;
        }
        has_resp = true;
        break;
    }
    case meshtastic_AdminMessage_set_canned_message_module_messages_tag:
        copyBounded(admin_canned_messages_,
                    sizeof(admin_canned_messages_),
                    req.set_canned_message_module_messages);
        resp.which_payload_variant = meshtastic_AdminMessage_get_canned_message_module_messages_response_tag;
        copyBounded(resp.get_canned_message_module_messages_response,
                    sizeof(resp.get_canned_message_module_messages_response),
                    admin_canned_messages_);
        has_resp = true;
        break;
    case meshtastic_AdminMessage_set_ringtone_message_tag:
        copyBounded(admin_ringtone_,
                    sizeof(admin_ringtone_),
                    req.set_ringtone_message);
        resp.which_payload_variant = meshtastic_AdminMessage_get_ringtone_response_tag;
        copyBounded(resp.get_ringtone_response,
                    sizeof(resp.get_ringtone_response),
                    admin_ringtone_);
        has_resp = true;
        break;
    case meshtastic_AdminMessage_store_ui_config_tag:
        resp.which_payload_variant = meshtastic_AdminMessage_get_ui_config_response_tag;
        resp.get_ui_config_response = buildDeviceUi();
        has_resp = true;
        break;
    case meshtastic_AdminMessage_set_time_only_tag:
    {
        return ctx_.syncCurrentEpochSeconds(static_cast<uint32_t>(req.set_time_only));
    }
    case meshtastic_AdminMessage_remove_by_nodenum_tag:
        return true;
    case meshtastic_AdminMessage_factory_reset_config_tag:
        ctx_.resetMeshConfig();
        ctx_.clearNodeDb();
        ctx_.clearMessageDb();
        return true;
    default:
        return false;
    }

    logDual("[BLE][mtcore] admin handled variant=%u has_resp=%u resp_variant=%u\n",
            static_cast<unsigned>(req.which_payload_variant),
            has_resp ? 1U : 0U,
            static_cast<unsigned>(resp.which_payload_variant));

    if (!has_resp)
    {
        return true;
    }

    auto& reply = reply_packet_scratch_;
    std::memset(&reply, 0, sizeof(reply));
    reply.from = ctx_.getSelfNodeId();
    reply.to = ctx_.getSelfNodeId();
    reply.channel = packet.channel;
    reply.id = sys::millis_now();
    reply.rx_time = nowSeconds();
    reply.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    reply.decoded = meshtastic_Data_init_zero;
    reply.decoded.portnum = meshtastic_PortNum_ADMIN_APP;
    reply.decoded.dest = reply.to;
    reply.decoded.source = reply.from;
    reply.decoded.request_id = packet.id;
    reply.decoded.want_response = false;
    reply.decoded.has_bitfield = true;
    reply.decoded.bitfield = 0;

    pb_ostream_t out_stream = pb_ostream_from_buffer(reply.decoded.payload.bytes, sizeof(reply.decoded.payload.bytes));
    if (!pb_encode(&out_stream, meshtastic_AdminMessage_fields, &resp))
    {
        return false;
    }
    reply.decoded.payload.size = static_cast<pb_size_t>(out_stream.bytes_written);
    packet_queue_.push_back(reply);
    notifyFromNum(reply.id);
    return true;
}

bool MeshtasticPhoneCore::handleLocalSelfPacket(meshtastic_MeshPacket& packet)
{
    const uint32_t self = ctx_.getSelfNodeId();
    if (self == 0 || packet.to != self || packet.which_payload_variant != meshtastic_MeshPacket_decoded_tag)
    {
        return false;
    }

    if (packet.decoded.portnum == meshtastic_PortNum_TELEMETRY_APP && packet.decoded.want_response &&
        packet.decoded.payload.size > 0)
    {
        meshtastic_Telemetry req = meshtastic_Telemetry_init_zero;
        pb_istream_t req_stream = pb_istream_from_buffer(packet.decoded.payload.bytes, packet.decoded.payload.size);
        if (!pb_decode(&req_stream, meshtastic_Telemetry_fields, &req))
        {
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
            return false;
        }

        auto& reply = reply_packet_scratch_;
        std::memset(&reply, 0, sizeof(reply));
        reply.from = self;
        reply.to = self;
        reply.channel = packet.channel;
        reply.id = sys::millis_now();
        reply.rx_time = nowSeconds();
        reply.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        reply.decoded = meshtastic_Data_init_zero;
        reply.decoded.portnum = meshtastic_PortNum_TELEMETRY_APP;
        reply.decoded.dest = self;
        reply.decoded.source = self;
        reply.decoded.request_id = packet.id;
        reply.decoded.want_response = false;
        reply.decoded.has_bitfield = true;
        reply.decoded.bitfield = 0;
        pb_ostream_t out_stream = pb_ostream_from_buffer(reply.decoded.payload.bytes, sizeof(reply.decoded.payload.bytes));
        if (!pb_encode(&out_stream, meshtastic_Telemetry_fields, &resp))
        {
            return false;
        }
        reply.decoded.payload.size = static_cast<pb_size_t>(out_stream.bytes_written);
        packet_queue_.push_back(reply);
        notifyFromNum(reply.id);
        return true;
    }

    if (packet.decoded.portnum == meshtastic_PortNum_POSITION_APP && packet.decoded.want_response)
    {
        auto& reply = reply_packet_scratch_;
        std::memset(&reply, 0, sizeof(reply));
        reply.from = self;
        reply.to = self;
        reply.channel = packet.channel;
        reply.id = sys::millis_now();
        reply.rx_time = nowSeconds();
        reply.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        reply.decoded = meshtastic_Data_init_zero;
        reply.decoded.portnum = meshtastic_PortNum_POSITION_APP;
        reply.decoded.dest = self;
        reply.decoded.source = self;
        reply.decoded.request_id = packet.id;
        reply.decoded.want_response = false;
        reply.decoded.has_bitfield = true;
        reply.decoded.bitfield = 0;
        size_t payload_len = sizeof(reply.decoded.payload.bytes);
        if (!buildSelfPositionPayload(device_runtime_hooks_, reply.decoded.payload.bytes, &payload_len))
        {
            logDual("[BLE][mtcore] self position unavailable, skip loopback tx req=%08lX\n",
                    static_cast<unsigned long>(packet.id));
            return true;
        }
        reply.decoded.payload.size = static_cast<pb_size_t>(payload_len);
        packet_queue_.push_back(reply);
        notifyFromNum(reply.id);
        return true;
    }

    if (packet.decoded.portnum == meshtastic_PortNum_NODEINFO_APP && packet.decoded.want_response)
    {
        meshtastic_NodeInfo self_info = buildSelfNodeInfo();
        auto& reply = reply_packet_scratch_;
        std::memset(&reply, 0, sizeof(reply));
        reply.from = self;
        reply.to = self;
        reply.channel = packet.channel;
        reply.id = sys::millis_now();
        reply.rx_time = nowSeconds();
        reply.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        reply.decoded = meshtastic_Data_init_zero;
        reply.decoded.portnum = meshtastic_PortNum_NODEINFO_APP;
        reply.decoded.dest = self;
        reply.decoded.source = self;
        reply.decoded.request_id = packet.id;
        reply.decoded.want_response = false;
        reply.decoded.has_bitfield = true;
        reply.decoded.bitfield = 0;
        pb_ostream_t out_stream = pb_ostream_from_buffer(reply.decoded.payload.bytes, sizeof(reply.decoded.payload.bytes));
        if (!pb_encode(&out_stream, meshtastic_User_fields, &self_info.user))
        {
            return false;
        }
        reply.decoded.payload.size = static_cast<pb_size_t>(out_stream.bytes_written);
        packet_queue_.push_back(reply);
        notifyFromNum(reply.id);
        return true;
    }

    if (packet.decoded.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP &&
        packet.decoded.portnum != meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP)
    {
        logDual("[BLE][mtcore] suppress self loopback port=%u want_resp=%u req=%08lX len=%u\n",
                static_cast<unsigned>(packet.decoded.portnum),
                packet.decoded.want_response ? 1U : 0U,
                static_cast<unsigned long>(packet.id),
                static_cast<unsigned>(packet.decoded.payload.size));
        return !packet.decoded.want_response;
    }

    return false;
}

void MeshtasticPhoneCore::pumpIncomingAppData()
{
    chat::IMeshAdapter* adapter = ctx_.getMeshAdapter();
    if (!adapter)
    {
        return;
    }

    for (uint8_t count = 0; count < kQueueDepthHint; ++count)
    {
        chat::MeshIncomingData incoming{};
        if (!adapter->pollIncomingData(&incoming))
        {
            break;
        }
        packet_queue_.push_back(buildPacketFromData(incoming));
        notifyFromNum(packet_queue_.back().id);
    }
}

bool MeshtasticPhoneCore::popToPhone(MeshtasticBleFrame* out)
{
    if (!out)
    {
        return false;
    }

    if (mqtt_hooks_)
    {
        auto& mqtt = mqtt_proxy_scratch_;
        std::memset(&mqtt, 0, sizeof(mqtt));
        if (mqtt_hooks_->pollMqttProxyToPhone(&mqtt))
        {
            auto& from = from_radio_scratch_;
            std::memset(&from, 0, sizeof(from));
            from.which_payload_variant = meshtastic_FromRadio_mqttClientProxyMessage_tag;
            from.mqttClientProxyMessage = mqtt;
            return encodeFromRadio(from, 0, out);
        }
    }

    if (!frame_queue_.empty())
    {
        *out = frame_queue_.front();
        frame_queue_.pop_front();
        return true;
    }

    if (config_flow_active_ && popConfigSnapshotFrame(out))
    {
        return true;
    }

    if (config_drain_empty_pending_)
    {
        config_drain_empty_pending_ = false;
        logDual("[BLE][mtcore] config snapshot drain-empty\n");
        return false;
    }

    auto& from = from_radio_scratch_;
    std::memset(&from, 0, sizeof(from));
    if (!queue_status_queue_.empty())
    {
        from.which_payload_variant = meshtastic_FromRadio_queueStatus_tag;
        from.queueStatus = queue_status_queue_.front();
        queue_status_queue_.pop_front();
        return encodeFromRadio(from, from.queueStatus.mesh_packet_id, out);
    }

    if (!packet_queue_.empty())
    {
        from.which_payload_variant = meshtastic_FromRadio_packet_tag;
        from.packet = packet_queue_.front();
        packet_queue_.pop_front();
        return encodeFromRadio(from, from.packet.id, out);
    }

    return false;
}

bool MeshtasticPhoneCore::popConfigSnapshotFrame(MeshtasticBleFrame* out)
{
    if (!out || !config_flow_active_)
    {
        return false;
    }

    auto& from = from_radio_scratch_;
    std::memset(&from, 0, sizeof(from));
    const uint32_t from_num = config_nonce_;

    if (config_node_index_ == 0)
    {
        from.which_payload_variant = meshtastic_FromRadio_my_info_tag;
        fillMyInfo(&from.my_info);
        ++config_node_index_;
        logDual("[BLE][mtcore][cfg#%lu] frame my_info nonce=%08lX\n",
                static_cast<unsigned long>(config_request_seq_),
                static_cast<unsigned long>(from_num));
        logRuntimeFootprint("cfg_my_info");
        return encodeFromRadio(from, from_num, out);
    }

    if (config_node_index_ == 1)
    {
        from.which_payload_variant = meshtastic_FromRadio_deviceuiConfig_tag;
        fillDeviceUi(&from.deviceuiConfig);
        ++config_node_index_;
        logDual("[BLE][mtcore][cfg#%lu] frame deviceui nonce=%08lX\n",
                static_cast<unsigned long>(config_request_seq_),
                static_cast<unsigned long>(from_num));
        logRuntimeFootprint("cfg_deviceui");
        return encodeFromRadio(from, from_num, out);
    }

    if (config_node_index_ == 2)
    {
        from.which_payload_variant = meshtastic_FromRadio_node_info_tag;
        fillSelfNodeInfo(&from.node_info);
        ++config_node_index_;
        logDual("[BLE][mtcore][cfg#%lu] frame self_node nonce=%08lX\n",
                static_cast<unsigned long>(config_request_seq_),
                static_cast<unsigned long>(from_num));
        logRuntimeFootprint("cfg_self_node");
        return encodeFromRadio(from, from_num, out);
    }

    if (const auto* store = ctx_.getNodeStore())
    {
        const auto& entries = store->getEntries();
        while ((config_node_index_ - 3) < entries.size())
        {
            const auto& entry = entries[config_node_index_ - 3];
            ++config_node_index_;
            if (entry.node_id == 0 || entry.node_id == ctx_.getSelfNodeId())
            {
                continue;
            }
            from.which_payload_variant = meshtastic_FromRadio_node_info_tag;
            fillNodeInfoFromEntry(entry, &from.node_info);
            return encodeFromRadio(from, entry.node_id, out);
        }
    }

    if (config_channel_index_ == 0)
    {
        from.which_payload_variant = meshtastic_FromRadio_metadata_tag;
        fillMetadata(&from.metadata);
        ++config_channel_index_;
        return encodeFromRadio(from, from_num, out);
    }

    const uint8_t channel_slot = static_cast<uint8_t>(config_channel_index_ - 1);
    if (channel_slot < meshtastic_defaults::kMaxMeshtasticChannels)
    {
        from.which_payload_variant = meshtastic_FromRadio_channel_tag;
        fillChannel(channel_slot, &from.channel);
        ++config_channel_index_;
        return encodeFromRadio(from, from_num, out);
    }

    if (config_type_index_ < (sizeof(kConfigSnapshotTypes) / sizeof(kConfigSnapshotTypes[0])))
    {
        from.which_payload_variant = meshtastic_FromRadio_config_tag;
        fillConfig(kConfigSnapshotTypes[config_type_index_++], &from.config);
        return encodeFromRadio(from, from_num, out);
    }

    if (config_module_type_index_ < (sizeof(kModuleSnapshotTypes) / sizeof(kModuleSnapshotTypes[0])))
    {
        from.which_payload_variant = meshtastic_FromRadio_moduleConfig_tag;
        fillModuleConfig(kModuleSnapshotTypes[config_module_type_index_++], &from.moduleConfig);
        return encodeFromRadio(from, from_num, out);
    }

    from.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
    from.config_complete_id = config_nonce_;
    const size_t completed_node_index = config_node_index_;
    const uint8_t completed_channel_index = config_channel_index_;
    const uint8_t completed_config_index = config_type_index_;
    const uint8_t completed_module_index = config_module_type_index_;
    config_flow_active_ = false;
    config_drain_empty_pending_ = true;
    config_nonce_ = 0;
    config_node_index_ = 0;
    config_channel_index_ = 0;
    config_type_index_ = 0;
    config_module_type_index_ = 0;
    if (config_lifecycle_hooks_)
    {
        config_lifecycle_hooks_->onConfigComplete();
    }
    logDual("[BLE][mtcore][cfg#%lu] complete nonce=%08lX nodes=%u channels=%u configs=%u modules=%u\n",
            static_cast<unsigned long>(config_request_seq_),
            static_cast<unsigned long>(from_num),
            static_cast<unsigned>(completed_node_index),
            static_cast<unsigned>(completed_channel_index),
            static_cast<unsigned>(completed_config_index),
            static_cast<unsigned>(completed_module_index));
    logDual("[BLE][mtcore][flow] cfg_complete stage=%s nonce=%08lX\n",
            configStageName(from_num),
            static_cast<unsigned long>(from_num));
    logRuntimeFootprint("cfg_complete");
    return encodeFromRadio(from, from_num, out);
}

bool MeshtasticPhoneCore::encodeFromRadio(const meshtastic_FromRadio& from, uint32_t from_num, MeshtasticBleFrame* out) const
{
    if (!out)
    {
        logDual("[BLE][mtcore] encode from_radio failed: out=null variant=%u from_num=%08lX\n",
                static_cast<unsigned>(from.which_payload_variant),
                static_cast<unsigned long>(from_num));
        return false;
    }

    pb_ostream_t ostream = pb_ostream_from_buffer(out->buf, sizeof(out->buf));
    if (!pb_encode(&ostream, meshtastic_FromRadio_fields, const_cast<meshtastic_FromRadio*>(&from)))
    {
        logDual("[BLE][mtcore] encode from_radio failed: variant=%u from_num=%08lX\n",
                static_cast<unsigned>(from.which_payload_variant),
                static_cast<unsigned long>(from_num));
        return false;
    }

    out->len = ostream.bytes_written;
    out->from_num = from_num;
    logDual("[BLE][mtcore] encode from_radio ok: variant=%u from_num=%08lX len=%u\n",
            static_cast<unsigned>(from.which_payload_variant),
            static_cast<unsigned long>(from_num),
            static_cast<unsigned>(out->len));
    return true;
}

void MeshtasticPhoneCore::enqueueQueueStatus(uint32_t packet_id, bool ok)
{
    meshtastic_QueueStatus status = meshtastic_QueueStatus_init_zero;
    status.res = ok ? 0 : 1;
    status.free = kQueueDepthHint;
    status.maxlen = kQueueDepthHint;
    status.mesh_packet_id = packet_id;
    queue_status_queue_.push_back(status);
    logDual("[BLE][mtcore] queue status mesh_packet_id=%08lX ok=%u depth=%u\n",
            static_cast<unsigned long>(packet_id),
            ok ? 1U : 0U,
            static_cast<unsigned>(queue_status_queue_.size()));
    notifyFromNum(packet_id);
}

void MeshtasticPhoneCore::enqueueConfigSnapshot(uint32_t config_nonce)
{
    ++config_request_seq_;
    config_flow_active_ = true;
    config_nonce_ = config_nonce;
    config_node_index_ = 0;
    config_channel_index_ = 0;
    config_type_index_ = 0;
    config_module_type_index_ = 0;
    frame_queue_.clear();
    if (config_lifecycle_hooks_)
    {
        config_lifecycle_hooks_->onConfigStart();
    }
    logDual("[BLE][mtcore][cfg#%lu] start nonce=%08lX\n",
            static_cast<unsigned long>(config_request_seq_),
            static_cast<unsigned long>(config_nonce));
    logDual("[BLE][mtcore][flow] cfg_start stage=%s nonce=%08lX\n",
            configStageName(config_nonce),
            static_cast<unsigned long>(config_nonce));
    logRuntimeFootprint("cfg_start");
    notifyFromNum(config_nonce);
}

void MeshtasticPhoneCore::enqueueFromRadio(const meshtastic_FromRadio& from, uint32_t from_num)
{
    MeshtasticBleFrame frame{};
    if (encodeFromRadio(from, from_num, &frame))
    {
        frame_queue_.push_back(frame);
    }
}

void MeshtasticPhoneCore::notifyFromNum(uint32_t from_num)
{
    transport_.notifyFromNum(from_num);
}

void MeshtasticPhoneCore::fillMyInfo(meshtastic_MyNodeInfo* out) const
{
    if (!out)
    {
        return;
    }
    meshtastic_MyNodeInfo& info = *out;
    std::memset(&info, 0, sizeof(info));
    info.my_node_num = ctx_.getSelfNodeId();
    info.reboot_count = 0;
    info.min_app_version = meshtastic_defaults::kOfficialMinAppVersion;

    size_t nodedb_count = 1;
    if (const auto* store = ctx_.getNodeStore())
    {
        nodedb_count += store->getEntries().size();
    }
    if (nodedb_count > 0xFFFFU)
    {
        nodedb_count = 0xFFFFU;
    }
    info.nodedb_count = static_cast<uint16_t>(nodedb_count);

    uint8_t mac[6] = {};
    const bool has_mac = ctx_.getDeviceMacAddress(mac);
    const size_t mac_len = has_mac ? sizeof(mac) : 0;
    std::memcpy(info.device_id.bytes, mac, mac_len);
    std::memcpy(info.device_id.bytes + mac_len, &info.my_node_num, sizeof(info.my_node_num));
    info.device_id.size = static_cast<pb_size_t>(mac_len + sizeof(info.my_node_num));

    copyBounded(info.pio_env, sizeof(info.pio_env), "Trail Mate");
    info.firmware_edition = meshtastic_FirmwareEdition_VANILLA;
}

meshtastic_MyNodeInfo MeshtasticPhoneCore::buildMyInfo() const
{
    meshtastic_MyNodeInfo info = meshtastic_MyNodeInfo_init_zero;
    fillMyInfo(&info);
    return info;
}

void MeshtasticPhoneCore::fillSelfNodeInfo(meshtastic_NodeInfo* out) const
{
    if (!out)
    {
        return;
    }
    meshtastic_NodeInfo& info = *out;
    std::memset(&info, 0, sizeof(info));
    info.num = ctx_.getSelfNodeId();
    info.has_user = true;

    char long_name[32] = {};
    char short_name[16] = {};
    ctx_.getEffectiveUserInfo(long_name, sizeof(long_name), short_name, sizeof(short_name));

    char user_id[16] = {};
    std::snprintf(user_id, sizeof(user_id), "!%08lX", static_cast<unsigned long>(ctx_.getSelfNodeId()));
    copyBounded(info.user.id, sizeof(info.user.id), user_id);
    copyBounded(info.user.long_name, sizeof(info.user.long_name), long_name);
    copyBounded(info.user.short_name, sizeof(info.user.short_name), short_name);
    info.user.hw_model = meshtastic_HardwareModel_UNSET;
    info.user.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    info.channel = 0;
    info.last_heard = nowSeconds();
    info.has_hops_away = true;
    info.hops_away = 0;
}

meshtastic_NodeInfo MeshtasticPhoneCore::buildSelfNodeInfo() const
{
    meshtastic_NodeInfo info = meshtastic_NodeInfo_init_zero;
    fillSelfNodeInfo(&info);
    return info;
}

void MeshtasticPhoneCore::fillNodeInfoFromEntry(const chat::contacts::NodeEntry& entry, meshtastic_NodeInfo* out) const
{
    if (!out)
    {
        return;
    }
    meshtastic_NodeInfo& info = *out;
    std::memset(&info, 0, sizeof(info));
    info.num = entry.node_id;
    info.has_user = true;

    char user_id[16] = {};
    std::snprintf(user_id, sizeof(user_id), "!%08lX", static_cast<unsigned long>(entry.node_id));
    copyBounded(info.user.id, sizeof(info.user.id), user_id);
    copyBounded(info.user.long_name, sizeof(info.user.long_name), entry.long_name);
    copyBounded(info.user.short_name, sizeof(info.user.short_name), entry.short_name);
    if (entry.has_macaddr)
    {
        memcpy(info.user.macaddr, entry.macaddr, sizeof(info.user.macaddr));
    }
    info.user.hw_model = static_cast<meshtastic_HardwareModel>(entry.hw_model);
    info.user.role = roleFromEntry(entry.role);
    info.channel = entry.channel;
    info.last_heard = entry.last_seen;
    info.snr = entry.snr;
    info.has_hops_away = (entry.hops_away != 0xFFU);
    info.hops_away = entry.hops_away;
    info.via_mqtt = entry.via_mqtt;
    info.is_ignored = entry.is_ignored;
    info.is_key_manually_verified = entry.key_manually_verified;
    if (entry.has_device_metrics)
    {
        info.has_device_metrics = true;
        info.device_metrics.has_battery_level = entry.device_metrics.has_battery_level;
        info.device_metrics.battery_level = entry.device_metrics.battery_level;
        info.device_metrics.has_voltage = entry.device_metrics.has_voltage;
        info.device_metrics.voltage = entry.device_metrics.voltage;
        info.device_metrics.has_channel_utilization = entry.device_metrics.has_channel_utilization;
        info.device_metrics.channel_utilization = entry.device_metrics.channel_utilization;
        info.device_metrics.has_air_util_tx = entry.device_metrics.has_air_util_tx;
        info.device_metrics.air_util_tx = entry.device_metrics.air_util_tx;
        info.device_metrics.has_uptime_seconds = entry.device_metrics.has_uptime_seconds;
        info.device_metrics.uptime_seconds = entry.device_metrics.uptime_seconds;
    }

    const chat::contacts::NodeInfo* node = ctx_.getContactService().getNodeInfo(entry.node_id);
    if (node != nullptr)
    {
        if (node->position.valid)
        {
            info.has_position = true;
            info.position = meshtastic_Position_init_zero;
            info.position.has_latitude_i = true;
            info.position.latitude_i = node->position.latitude_i;
            info.position.has_longitude_i = true;
            info.position.longitude_i = node->position.longitude_i;
            info.position.timestamp = node->position.timestamp;
            info.position.has_altitude = node->position.has_altitude;
            info.position.altitude = node->position.altitude;
            info.position.precision_bits = node->position.precision_bits;
            info.position.PDOP = node->position.pdop;
            info.position.HDOP = node->position.hdop;
            info.position.VDOP = node->position.vdop;
            info.position.gps_accuracy = node->position.gps_accuracy_mm;
        }
    }
}

meshtastic_NodeInfo MeshtasticPhoneCore::buildNodeInfoFromEntry(const chat::contacts::NodeEntry& entry) const
{
    meshtastic_NodeInfo info = meshtastic_NodeInfo_init_zero;
    fillNodeInfoFromEntry(entry, &info);
    return info;
}

void MeshtasticPhoneCore::fillMetadata(meshtastic_DeviceMetadata* out) const
{
    if (!out)
    {
        return;
    }
    meshtastic_DeviceMetadata& metadata = *out;
    std::memset(&metadata, 0, sizeof(metadata));
    copyBounded(metadata.firmware_version, sizeof(metadata.firmware_version), meshtastic_defaults::kCompatFirmwareVersion);
    metadata.device_state_version = meshtastic_defaults::kOfficialDeviceStateVersion;
    metadata.canShutdown = true;
    metadata.hasBluetooth = true;
    metadata.hasWifi = false;
    metadata.hasEthernet = false;
    metadata.hasRemoteHardware = false;
#if defined(NRF52840_XXAA) || defined(ARDUINO_NRF52840_FEATHER)
    // The nRF52 radio backend can use PKI on-air, but the phone-side BLE admin
    // flow does not yet fully model the official PKI management exchange.
    // Advertising PKC here causes some phone apps to enter unsupported init paths
    // immediately after pairing.
    metadata.hasPKC = false;
#else
    metadata.hasPKC = ctx_.getMeshAdapter() ? ctx_.getMeshAdapter()->isPkiReady() : false;
#endif
    metadata.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    metadata.position_flags = 0;
    metadata.hw_model = meshtastic_HardwareModel_UNSET;
    metadata.excluded_modules = 0;
}

meshtastic_DeviceMetadata MeshtasticPhoneCore::buildMetadata() const
{
    meshtastic_DeviceMetadata metadata = meshtastic_DeviceMetadata_init_zero;
    fillMetadata(&metadata);
    return metadata;
}

meshtastic_DeviceMetrics MeshtasticPhoneCore::buildDeviceMetrics() const
{
    meshtastic_DeviceMetrics metrics = meshtastic_DeviceMetrics_init_zero;
    metrics.has_uptime_seconds = true;
    metrics.uptime_seconds = sys::uptime_seconds_now();
    return metrics;
}

meshtastic_LocalStats MeshtasticPhoneCore::buildLocalStats() const
{
    meshtastic_LocalStats stats = meshtastic_LocalStats_init_zero;
    stats.uptime_seconds = sys::uptime_seconds_now();
    stats.channel_utilization = 0.0f;
    stats.air_util_tx = 0.0f;
    stats.num_packets_tx = 0;
    stats.num_packets_rx = 0;
    stats.num_packets_rx_bad = 0;
    stats.num_rx_dupe = 0;
    stats.num_tx_relay = 0;
    stats.num_tx_relay_canceled = 0;
    stats.num_tx_dropped = 0;
    stats.heap_total_bytes = 0;
    stats.heap_free_bytes = 0;
    if (const auto* store = ctx_.getNodeStore())
    {
        const size_t total = store->getEntries().size();
        stats.num_total_nodes = static_cast<uint16_t>(std::min<size_t>(total, 0xFFFFU));
        stats.num_online_nodes = stats.num_total_nodes;
    }
    return stats;
}

void MeshtasticPhoneCore::fillDeviceUi(meshtastic_DeviceUIConfig* out) const
{
    if (!out)
    {
        return;
    }
    meshtastic_DeviceUIConfig& ui = *out;
    std::memset(&ui, 0, sizeof(ui));
    ui.version = 1;
    ui.screen_brightness = 255;
    ui.screen_timeout = 30;
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
}

meshtastic_DeviceUIConfig MeshtasticPhoneCore::buildDeviceUi() const
{
    meshtastic_DeviceUIConfig ui = meshtastic_DeviceUIConfig_init_zero;
    fillDeviceUi(&ui);
    return ui;
}

void MeshtasticPhoneCore::fillChannel(uint8_t idx, meshtastic_Channel* out) const
{
    if (!out)
    {
        return;
    }
    meshtastic_Channel& channel = *out;
    std::memset(&channel, 0, sizeof(channel));
    channel.index = idx;

    const auto cfg = ctx_.getMeshtasticPhoneConfig();
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
        return;
    }

    channel.has_settings = true;
    {
        meshtastic_ChannelSettings settings = meshtastic_ChannelSettings_init_zero;
        channel.settings = settings;
    }
    channel.settings.channel_num = idx;
    channel.settings.id = 0;
    channel.settings.uplink_enabled = (idx == 0) ? cfg.primary_uplink_enabled : cfg.secondary_uplink_enabled;
    channel.settings.downlink_enabled = (idx == 0) ? cfg.primary_downlink_enabled : cfg.secondary_downlink_enabled;
    channel.settings.has_module_settings = false;

    if (idx == 0)
    {
        copyBounded(channel.settings.name, sizeof(channel.settings.name), "Primary");
        if (cfg.mesh.primary_key[0] != 0)
        {
            channel.settings.psk.size = sizeof(cfg.mesh.primary_key);
            std::memcpy(channel.settings.psk.bytes,
                        cfg.mesh.primary_key,
                        sizeof(cfg.mesh.primary_key));
        }
        else
        {
            channel.settings.psk.size = 1;
            channel.settings.psk.bytes[0] = 1;
        }
    }
    else
    {
        copyBounded(channel.settings.name, sizeof(channel.settings.name), "Secondary");
        if (cfg.mesh.secondary_key[0] != 0)
        {
            channel.settings.psk.size = sizeof(cfg.mesh.secondary_key);
            std::memcpy(channel.settings.psk.bytes,
                        cfg.mesh.secondary_key,
                        sizeof(cfg.mesh.secondary_key));
        }
    }
}

meshtastic_Channel MeshtasticPhoneCore::buildChannel(uint8_t idx) const
{
    meshtastic_Channel channel = meshtastic_Channel_init_zero;
    fillChannel(idx, &channel);
    return channel;
}

void MeshtasticPhoneCore::fillConfig(meshtastic_AdminMessage_ConfigType type, meshtastic_Config* out) const
{
    if (!out)
    {
        return;
    }
    const auto cfg = ctx_.getMeshtasticPhoneConfig();
    std::memset(out, 0, sizeof(*out));
    meshtastic_Config& cfg_out = *out;
    switch (type)
    {
    case meshtastic_AdminMessage_ConfigType_DEVICE_CONFIG:
        cfg_out.which_payload_variant = meshtastic_Config_device_tag;
        {
            meshtastic_Config_DeviceConfig device = meshtastic_Config_DeviceConfig_init_zero;
            cfg_out.payload_variant.device = device;
        }
        cfg_out.payload_variant.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
        cfg_out.payload_variant.device.rebroadcast_mode =
            cfg.relay_enabled ? meshtastic_Config_DeviceConfig_RebroadcastMode_ALL
                              : meshtastic_Config_DeviceConfig_RebroadcastMode_NONE;
        cfg_out.payload_variant.device.node_info_broadcast_secs = 900;
        cfg_out.payload_variant.device.serial_enabled = false;
        cfg_out.payload_variant.device.is_managed = false;
        cfg_out.payload_variant.device.led_heartbeat_disabled = false;
        cfg_out.payload_variant.device.buzzer_mode = meshtastic_Config_DeviceConfig_BuzzerMode_DISABLED;
        buildLinkedTimezoneTzdef(device_runtime_hooks_,
                                 cfg_out.payload_variant.device.tzdef,
                                 sizeof(cfg_out.payload_variant.device.tzdef));
        break;
    case meshtastic_AdminMessage_ConfigType_POSITION_CONFIG:
        cfg_out.which_payload_variant = meshtastic_Config_position_tag;
        {
            meshtastic_Config_PositionConfig position = meshtastic_Config_PositionConfig_init_zero;
            cfg_out.payload_variant.position = position;
        }
        cfg_out.payload_variant.position.position_broadcast_secs = 900;
        cfg_out.payload_variant.position.gps_enabled = cfg.gps_enabled;
        cfg_out.payload_variant.position.gps_update_interval = cfg.gps_interval_ms / 1000U;
        cfg_out.payload_variant.position.gps_mode = cfg.gps_enabled
                                                        ? meshtastic_Config_PositionConfig_GpsMode_ENABLED
                                                        : meshtastic_Config_PositionConfig_GpsMode_DISABLED;
        break;
    case meshtastic_AdminMessage_ConfigType_DISPLAY_CONFIG:
        cfg_out.which_payload_variant = meshtastic_Config_display_tag;
        {
            meshtastic_Config_DisplayConfig display = meshtastic_Config_DisplayConfig_init_zero;
            cfg_out.payload_variant.display = display;
        }
        cfg_out.payload_variant.display.screen_on_secs = 30;
        cfg_out.payload_variant.display.units = meshtastic_Config_DisplayConfig_DisplayUnits_METRIC;
        cfg_out.payload_variant.display.oled = meshtastic_Config_DisplayConfig_OledType_OLED_AUTO;
        cfg_out.payload_variant.display.displaymode = meshtastic_Config_DisplayConfig_DisplayMode_DEFAULT;
        cfg_out.payload_variant.display.compass_orientation = meshtastic_Config_DisplayConfig_CompassOrientation_DEGREES_0;
        break;
    case meshtastic_AdminMessage_ConfigType_LORA_CONFIG:
        cfg_out.which_payload_variant = meshtastic_Config_lora_tag;
        {
            meshtastic_Config_LoRaConfig lora = meshtastic_Config_LoRaConfig_init_zero;
            cfg_out.payload_variant.lora = lora;
        }
        cfg_out.payload_variant.lora.use_preset = cfg.mesh.use_preset;
        cfg_out.payload_variant.lora.modem_preset =
            static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(cfg.mesh.modem_preset);
        cfg_out.payload_variant.lora.bandwidth = static_cast<uint32_t>(cfg.mesh.bandwidth_khz);
        cfg_out.payload_variant.lora.spread_factor = cfg.mesh.spread_factor;
        cfg_out.payload_variant.lora.coding_rate = cfg.mesh.coding_rate;
        cfg_out.payload_variant.lora.frequency_offset = cfg.mesh.frequency_offset_mhz;
        cfg_out.payload_variant.lora.region =
            static_cast<meshtastic_Config_LoRaConfig_RegionCode>(cfg.mesh.region);
        cfg_out.payload_variant.lora.hop_limit = cfg.mesh.hop_limit;
        cfg_out.payload_variant.lora.tx_enabled = cfg.mesh.tx_enabled;
        cfg_out.payload_variant.lora.tx_power = cfg.mesh.tx_power;
        cfg_out.payload_variant.lora.channel_num = cfg.mesh.channel_num;
        cfg_out.payload_variant.lora.override_duty_cycle = cfg.mesh.override_duty_cycle;
        cfg_out.payload_variant.lora.override_frequency = cfg.mesh.override_frequency_mhz;
        cfg_out.payload_variant.lora.ignore_mqtt = cfg.mesh.ignore_mqtt;
        cfg_out.payload_variant.lora.config_ok_to_mqtt = cfg.mesh.config_ok_to_mqtt;
        break;
    case meshtastic_AdminMessage_ConfigType_BLUETOOTH_CONFIG:
        cfg_out.which_payload_variant = meshtastic_Config_bluetooth_tag;
        {
            meshtastic_Config_BluetoothConfig bluetooth = meshtastic_Config_BluetoothConfig_init_zero;
            cfg_out.payload_variant.bluetooth = bluetooth;
        }
        cfg_out.payload_variant.bluetooth = bluetooth_config_;
        cfg_out.payload_variant.bluetooth.enabled = ctx_.isBleEnabled();
        break;
    case meshtastic_AdminMessage_ConfigType_SECURITY_CONFIG:
        cfg_out.which_payload_variant = meshtastic_Config_security_tag;
        {
            meshtastic_Config_SecurityConfig security = meshtastic_Config_SecurityConfig_init_zero;
            cfg_out.payload_variant.security = security;
        }
        cfg_out.payload_variant.security.is_managed = false;
        cfg_out.payload_variant.security.serial_enabled = false;
        cfg_out.payload_variant.security.debug_log_api_enabled = false;
        cfg_out.payload_variant.security.admin_channel_enabled = false;
        break;
    case meshtastic_AdminMessage_ConfigType_DEVICEUI_CONFIG:
        cfg_out.which_payload_variant = meshtastic_Config_device_ui_tag;
        fillDeviceUi(&cfg_out.payload_variant.device_ui);
        break;
    default:
        cfg_out.which_payload_variant = meshtastic_Config_device_tag;
        {
            meshtastic_Config_DeviceConfig device = meshtastic_Config_DeviceConfig_init_zero;
            cfg_out.payload_variant.device = device;
        }
        break;
    }
}

meshtastic_Config MeshtasticPhoneCore::buildConfig(meshtastic_AdminMessage_ConfigType type) const
{
    meshtastic_Config out = meshtastic_Config_init_zero;
    fillConfig(type, &out);
    return out;
}

void MeshtasticPhoneCore::fillModuleConfig(meshtastic_AdminMessage_ModuleConfigType type, meshtastic_ModuleConfig* out) const
{
    if (!out)
    {
        return;
    }
    std::memset(out, 0, sizeof(*out));
    meshtastic_ModuleConfig& module = *out;
    switch (type)
    {
    case meshtastic_AdminMessage_ModuleConfigType_MQTT_CONFIG:
        module.which_payload_variant = meshtastic_ModuleConfig_mqtt_tag;
        module.payload_variant.mqtt = module_config_.mqtt;
        break;
    case meshtastic_AdminMessage_ModuleConfigType_SERIAL_CONFIG:
        module.which_payload_variant = meshtastic_ModuleConfig_serial_tag;
        module.payload_variant.serial = module_config_.serial;
        break;
    case meshtastic_AdminMessage_ModuleConfigType_EXTNOTIF_CONFIG:
        module.which_payload_variant = meshtastic_ModuleConfig_external_notification_tag;
        module.payload_variant.external_notification = module_config_.external_notification;
        break;
    case meshtastic_AdminMessage_ModuleConfigType_STOREFORWARD_CONFIG:
        module.which_payload_variant = meshtastic_ModuleConfig_store_forward_tag;
        module.payload_variant.store_forward = module_config_.store_forward;
        break;
    case meshtastic_AdminMessage_ModuleConfigType_RANGETEST_CONFIG:
        module.which_payload_variant = meshtastic_ModuleConfig_range_test_tag;
        module.payload_variant.range_test = module_config_.range_test;
        break;
    case meshtastic_AdminMessage_ModuleConfigType_TELEMETRY_CONFIG:
        module.which_payload_variant = meshtastic_ModuleConfig_telemetry_tag;
        module.payload_variant.telemetry = module_config_.telemetry;
        break;
    case meshtastic_AdminMessage_ModuleConfigType_CANNEDMSG_CONFIG:
        module.which_payload_variant = meshtastic_ModuleConfig_canned_message_tag;
        module.payload_variant.canned_message = module_config_.canned_message;
        break;
    case meshtastic_AdminMessage_ModuleConfigType_AUDIO_CONFIG:
        module.which_payload_variant = meshtastic_ModuleConfig_audio_tag;
        module.payload_variant.audio = module_config_.audio;
        break;
    case meshtastic_AdminMessage_ModuleConfigType_REMOTEHARDWARE_CONFIG:
        module.which_payload_variant = meshtastic_ModuleConfig_remote_hardware_tag;
        module.payload_variant.remote_hardware = module_config_.remote_hardware;
        break;
    case meshtastic_AdminMessage_ModuleConfigType_NEIGHBORINFO_CONFIG:
        module.which_payload_variant = meshtastic_ModuleConfig_neighbor_info_tag;
        module.payload_variant.neighbor_info = module_config_.neighbor_info;
        break;
    case meshtastic_AdminMessage_ModuleConfigType_AMBIENTLIGHTING_CONFIG:
        module.which_payload_variant = meshtastic_ModuleConfig_ambient_lighting_tag;
        module.payload_variant.ambient_lighting = module_config_.ambient_lighting;
        break;
    case meshtastic_AdminMessage_ModuleConfigType_DETECTIONSENSOR_CONFIG:
        module.which_payload_variant = meshtastic_ModuleConfig_detection_sensor_tag;
        module.payload_variant.detection_sensor = module_config_.detection_sensor;
        break;
    case meshtastic_AdminMessage_ModuleConfigType_PAXCOUNTER_CONFIG:
        module.which_payload_variant = meshtastic_ModuleConfig_paxcounter_tag;
        module.payload_variant.paxcounter = module_config_.paxcounter;
        break;
    default:
        break;
    }
}

meshtastic_ModuleConfig MeshtasticPhoneCore::buildModuleConfig(meshtastic_AdminMessage_ModuleConfigType type) const
{
    meshtastic_ModuleConfig out = meshtastic_ModuleConfig_init_zero;
    fillModuleConfig(type, &out);
    return out;
}

meshtastic_MeshPacket MeshtasticPhoneCore::buildPacketFromText(const chat::MeshIncomingText& msg) const
{
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    packet.from = msg.from;
    packet.to = msg.to;
    packet.channel = channelIndexFromId(msg.channel);
    packet.id = (msg.msg_id == 0) ? sys::millis_now() : msg.msg_id;
    packet.rx_time = (msg.rx_meta.rx_timestamp_s != 0) ? msg.rx_meta.rx_timestamp_s : msg.timestamp;
    packet.rx_snr = msg.rx_meta.snr_db_x10 / 10.0f;
    packet.rx_rssi = msg.rx_meta.rssi_dbm_x10 / 10;
    packet.hop_limit = msg.hop_limit;
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    packet.decoded = meshtastic_Data_init_zero;
    packet.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    packet.decoded.source = msg.from;
    packet.decoded.dest = msg.to;
    packet.decoded.want_response = false;
    packet.decoded.has_bitfield = true;
    packet.decoded.bitfield = 0;
    packet.decoded.payload.size = static_cast<pb_size_t>(
        std::min(msg.text.size(), sizeof(packet.decoded.payload.bytes)));
    if (packet.decoded.payload.size > 0)
    {
        std::memcpy(packet.decoded.payload.bytes, msg.text.data(), packet.decoded.payload.size);
    }
    return packet;
}

meshtastic_MeshPacket MeshtasticPhoneCore::buildPacketFromData(const chat::MeshIncomingData& msg) const
{
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    packet.from = msg.from;
    packet.to = msg.to;
    packet.channel = channelIndexFromId(msg.channel);
    if (msg.packet_id != 0)
    {
        packet.id = msg.packet_id;
    }
    else if (msg.portnum == meshtastic_PortNum_ROUTING_APP && msg.request_id != 0)
    {
        // Keep synthetic routing/ack packets tied to the original request ID so
        // Meshtastic phone clients can correlate them with the pending send.
        packet.id = msg.request_id;
    }
    else
    {
        packet.id = sys::millis_now();
    }
    packet.rx_time = (msg.rx_meta.rx_timestamp_s != 0) ? msg.rx_meta.rx_timestamp_s : nowSeconds();
    packet.rx_snr = msg.rx_meta.snr_db_x10 / 10.0f;
    packet.rx_rssi = msg.rx_meta.rssi_dbm_x10 / 10;
    packet.hop_limit = msg.hop_limit;
    packet.relay_node = msg.rx_meta.relay_node;
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    packet.decoded = meshtastic_Data_init_zero;
    packet.decoded.portnum = static_cast<meshtastic_PortNum>(msg.portnum);
    packet.decoded.source = msg.from;
    packet.decoded.dest = msg.to;
    packet.decoded.request_id = msg.request_id;
    packet.decoded.want_response = msg.want_response;
    packet.decoded.has_bitfield = true;
    packet.decoded.bitfield = 0;
    packet.decoded.payload.size = static_cast<pb_size_t>(
        std::min(msg.payload.size(), sizeof(packet.decoded.payload.bytes)));
    if (packet.decoded.payload.size > 0)
    {
        std::memcpy(packet.decoded.payload.bytes, msg.payload.data(), packet.decoded.payload.size);
    }
    return packet;
}

} // namespace ble

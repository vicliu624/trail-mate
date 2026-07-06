#include "phone/meshtastic/meshtastic_phone_core.h"

#include "chat/infra/meshtastic/mt_protocol_helpers.h"
#include "chat/infra/meshtastic/mt_radio_config.h"
#include "chat/runtime/self_identity_policy.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "phone/meshtastic/meshtastic_defaults.h"
#include "phone/meshtastic/meshtastic_phone_config_bridge.h"
#include "platform/ui/timezone_profile.h"
#include "sys/clock.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace phone::meshtastic
{
namespace
{

constexpr meshtastic_AdminMessage_ConfigType kConfigSnapshotTypes[] = {
    meshtastic_AdminMessage_ConfigType_DEVICE_CONFIG,
    meshtastic_AdminMessage_ConfigType_POSITION_CONFIG,
    meshtastic_AdminMessage_ConfigType_POWER_CONFIG,
    meshtastic_AdminMessage_ConfigType_NETWORK_CONFIG,
    meshtastic_AdminMessage_ConfigType_DISPLAY_CONFIG,
    meshtastic_AdminMessage_ConfigType_LORA_CONFIG,
    meshtastic_AdminMessage_ConfigType_BLUETOOTH_CONFIG,
    meshtastic_AdminMessage_ConfigType_SECURITY_CONFIG,
    meshtastic_AdminMessage_ConfigType_SESSIONKEY_CONFIG,
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
    meshtastic_AdminMessage_ModuleConfigType_DETECTIONSENSOR_CONFIG,
    meshtastic_AdminMessage_ModuleConfigType_AMBIENTLIGHTING_CONFIG,
    meshtastic_AdminMessage_ModuleConfigType_PAXCOUNTER_CONFIG,
};

void logDual(const char* format, ...)
{
    if (!format)
    {
        return;
    }

    char buffer[160] = {};
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

bool hasBoundedText(const char* text, size_t max_len)
{
    return text && max_len > 0 && text[0] != '\0';
}

bool hasAndroidVisibleNodeName(const PhoneNodeView& entry)
{
    return hasBoundedText(entry.short_name, sizeof(entry.short_name)) ||
           hasBoundedText(entry.long_name, sizeof(entry.long_name));
}

void mixProjectionByte(uint32_t& hash, uint8_t value)
{
    hash ^= value;
    hash *= 16777619UL;
}

void mixProjectionString(uint32_t& hash, const char* text, size_t max_len)
{
    if (!text)
    {
        mixProjectionByte(hash, 0);
        return;
    }
    size_t index = 0;
    while (index < max_len && text[index] != '\0')
    {
        mixProjectionByte(hash, static_cast<uint8_t>(text[index]));
        ++index;
    }
    mixProjectionByte(hash, 0);
}

void mixProjectionU32(uint32_t& hash, uint32_t value)
{
    mixProjectionByte(hash, static_cast<uint8_t>(value & 0xFFU));
    mixProjectionByte(hash, static_cast<uint8_t>((value >> 8U) & 0xFFU));
    mixProjectionByte(hash, static_cast<uint8_t>((value >> 16U) & 0xFFU));
    mixProjectionByte(hash, static_cast<uint8_t>((value >> 24U) & 0xFFU));
}

void mixProjectionBytes(uint32_t& hash, const uint8_t* data, size_t len)
{
    if (!data)
    {
        mixProjectionByte(hash, 0);
        return;
    }
    for (size_t index = 0; index < len; ++index)
    {
        mixProjectionByte(hash, data[index]);
    }
    mixProjectionByte(hash, 0);
}

uint32_t nodeProjectionSignature(const PhoneNodeView& entry)
{
    uint32_t hash = 2166136261UL;
    mixProjectionU32(hash, entry.node_id);
    mixProjectionString(hash, entry.short_name, sizeof(entry.short_name));
    mixProjectionString(hash, entry.long_name, sizeof(entry.long_name));
    mixProjectionByte(hash, entry.protocol);
    mixProjectionByte(hash, entry.role);
    mixProjectionByte(hash, entry.hw_model);
    mixProjectionByte(hash, entry.channel);
    mixProjectionByte(hash, entry.via_mqtt ? 1U : 0U);
    mixProjectionByte(hash, entry.is_ignored ? 1U : 0U);
    mixProjectionByte(hash, entry.has_public_key ? 1U : 0U);
    mixProjectionByte(hash, entry.key_manually_verified ? 1U : 0U);
    return hash == 0 ? 1U : hash;
}

uint32_t nodeProjectionSignature(const chat::meshtastic::DecodedNodePayload& node)
{
    uint32_t hash = 2166136261UL;
    mixProjectionU32(hash, node.node_id);
    mixProjectionString(hash, node.short_name.c_str(), node.short_name.size() + 1U);
    mixProjectionString(hash, node.long_name.c_str(), node.long_name.size() + 1U);
    mixProjectionByte(hash, node.protocol);
    mixProjectionByte(hash, node.role);
    mixProjectionByte(hash, node.hw_model);
    mixProjectionByte(hash, node.channel);
    mixProjectionByte(hash, node.via_mqtt ? 1U : 0U);
    mixProjectionByte(hash, node.is_ignored ? 1U : 0U);
    mixProjectionByte(hash, node.has_public_key_state ? 1U : 0U);
    mixProjectionByte(hash, node.has_public_key ? 1U : 0U);
    if (node.has_public_key)
    {
        mixProjectionBytes(hash, node.public_key.data(), node.public_key.size());
    }
    mixProjectionByte(hash, node.key_manually_verified ? 1U : 0U);
    return hash == 0 ? 1U : hash;
}

uint32_t outputCoalesceKey(uint32_t category, uint32_t first, uint32_t second)
{
    uint32_t hash = 2166136261UL;
    mixProjectionU32(hash, category);
    mixProjectionU32(hash, first);
    mixProjectionU32(hash, second);
    return hash == 0 ? 1U : hash;
}

bool isPhoneTextPort(meshtastic_PortNum portnum)
{
    return portnum == meshtastic_PortNum_TEXT_MESSAGE_APP ||
           portnum == meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP ||
           portnum == meshtastic_PortNum_ALERT_APP;
}

bool isLowPriorityStatePort(meshtastic_PortNum portnum)
{
    return portnum == meshtastic_PortNum_POSITION_APP ||
           portnum == meshtastic_PortNum_TELEMETRY_APP ||
           portnum == meshtastic_PortNum_NODEINFO_APP;
}

void applyChannelPsk(uint8_t* dst,
                     size_t dst_len,
                     uint8_t* dst_key_len,
                     const meshtastic_ChannelSettings_psk_t& psk)
{
    if (!dst || !dst_key_len || dst_len == 0)
    {
        return;
    }
    std::memset(dst, 0, dst_len);
    *dst_key_len = 0;
    if (psk.size == 16 || psk.size == 32)
    {
        const size_t copy_len = std::min(static_cast<size_t>(psk.size), dst_len);
        std::memcpy(dst, psk.bytes, copy_len);
        if (copy_len == 16 || copy_len == 32)
        {
            *dst_key_len = static_cast<uint8_t>(copy_len);
        }
    }
    else if (psk.size == 1)
    {
        size_t expanded_len = 0;
        chat::meshtastic::expandShortPsk(psk.bytes[0], dst, &expanded_len);
        if (expanded_len == 16 && expanded_len <= dst_len)
        {
            *dst_key_len = static_cast<uint8_t>(expanded_len);
        }
    }
}

bool shortPskIndexForExpandedKey(const uint8_t* key, size_t key_len, uint8_t* out_index)
{
    if (!key || key_len != chat::kMeshtasticChannelKeyDefaultLen || !out_index)
    {
        return false;
    }

    uint8_t expanded[chat::kMeshtasticChannelKeyDefaultLen] = {};
    for (unsigned index = 1; index <= 255; ++index)
    {
        size_t expanded_len = 0;
        chat::meshtastic::expandShortPsk(static_cast<uint8_t>(index), expanded, &expanded_len);
        if (expanded_len == key_len && std::memcmp(expanded, key, key_len) == 0)
        {
            *out_index = static_cast<uint8_t>(index);
            return true;
        }
    }
    return false;
}

size_t meshtasticKeyLen(const uint8_t* key, size_t key_capacity, uint8_t stored_len)
{
    return chat::normalizeMeshtasticChannelKeyLen(key, key_capacity, stored_len);
}

void logChannelSummary(const char* prefix, const meshtastic_Channel& channel)
{
    const char* name = channel.has_settings ? channel.settings.name : "<none>";
    const unsigned psk_size = channel.has_settings ? static_cast<unsigned>(channel.settings.psk.size) : 0U;
    const unsigned channel_num = channel.has_settings ? static_cast<unsigned>(channel.settings.channel_num) : 0U;
    const unsigned id = channel.has_settings ? static_cast<unsigned>(channel.settings.id) : 0U;
    const unsigned uplink = channel.has_settings && channel.settings.uplink_enabled ? 1U : 0U;
    const unsigned downlink = channel.has_settings && channel.settings.downlink_enabled ? 1U : 0U;
    const bool has_module_settings = channel.has_settings && channel.settings.has_module_settings;
    const uint32_t position_precision = has_module_settings ? channel.settings.module_settings.position_precision : 0U;
    const unsigned is_muted = has_module_settings && channel.settings.module_settings.is_muted ? 1U : 0U;
    logDual("[BLE][mtcore][channel] %s idx=%d role=%u has_settings=%u ch_num=%u name=%s id=%u psk_size=%u uplink=%u downlink=%u module=%u pos_prec=%lu muted=%u\n",
            prefix ? prefix : "channel",
            static_cast<int>(channel.index),
            static_cast<unsigned>(channel.role),
            channel.has_settings ? 1U : 0U,
            channel_num,
            name,
            id,
            psk_size,
            uplink,
            downlink,
            has_module_settings ? 1U : 0U,
            static_cast<unsigned long>(position_precision),
            is_muted);
}

bool parsePosixTzOffsetMinutes(const char* tzdef, int* out_offset_min)
{
    return ::platform::ui::time::parse_posix_tz_standard_offset_minutes(tzdef, out_offset_min);
}

void buildFixedPosixTzdef(int offset_min, char* out, size_t out_len)
{
    ::platform::ui::time::build_fixed_posix_tzdef(offset_min, out, out_len);
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
    const auto* current_profile = device_runtime_hooks
                                      ? ::platform::ui::time::timezone_profile_by_id(
                                            device_runtime_hooks->getTimezoneProfileId())
                                      : nullptr;
    if (current_profile && current_profile->tzdef && current_profile->tzdef[0] != '\0')
    {
        copyBounded(out, out_len, current_profile->tzdef);
        return;
    }
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
    case defaults::kConfigNonceOnlyConfig:
        return "stage1_config";
    case defaults::kConfigNonceOnlyNodes:
        return "stage2_nodes";
    default:
        return "stage_unknown";
    }
}

const char* phoneApiPhaseName(MeshtasticPhoneCore::PhoneApiPhase phase)
{
    switch (phase)
    {
    case MeshtasticPhoneCore::PhoneApiPhase::SendNothing:
        return "SendNothing";
    case MeshtasticPhoneCore::PhoneApiPhase::ConfigFlow:
        return "ConfigFlow";
    case MeshtasticPhoneCore::PhoneApiPhase::SendPackets:
        return "SendPackets";
    default:
        return "Unknown";
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

meshtastic_HardwareModel localHardwareModel()
{
#if defined(ARDUINO_T_DECK_PRO)
    return meshtastic_HardwareModel_T_DECK_PRO;
#elif defined(ARDUINO_T_DECK)
    return meshtastic_HardwareModel_T_DECK;
#elif defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_LILYGO_LORA_SX1262) || \
    defined(ARDUINO_LILYGO_LORA_SX1280) || defined(ARDUINO_LILYGO_LORA_LR1121)
    return meshtastic_HardwareModel_T_LORA_PAGER;
#elif defined(ARDUINO_LILYGO_TWATCH_S3)
    return meshtastic_HardwareModel_T_WATCH_S3;
#elif defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return meshtastic_HardwareModel_MESH_TAB;
#else
    return meshtastic_HardwareModel_PRIVATE_HW;
#endif
}

void initDefaultModuleConfig(meshtastic_LocalModuleConfig* out, uint32_t self_node)
{
    config_bridge::initDefaultModuleConfig(out, self_node);
    if (out)
    {
        out->ambient_lighting.current = 8;
    }
}

void applyLegacyMqttDefaults(meshtastic_LocalModuleConfig* out)
{
    config_bridge::normalizeModuleConfig(out);
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

MeshtasticPhoneCore::MeshtasticPhoneCore(IPhoneAppFacade& app,
                                         MeshtasticPhoneTransport& transport,
                                         MeshtasticPhoneBluetoothConfigHooks* bluetooth_config_hooks,
                                         MeshtasticPhoneModuleConfigHooks* module_config_hooks,
                                         MeshtasticPhoneConfigLifecycleHooks* config_lifecycle_hooks,
                                         MeshtasticPhoneStatusHooks* status_hooks,
                                         MeshtasticPhoneMqttHooks* mqtt_hooks,
                                         MeshtasticPhoneDeviceRuntimeHooks* device_runtime_hooks)
    : app_(app),
      transport_(transport),
      bluetooth_config_hooks_(bluetooth_config_hooks),
      module_config_hooks_(module_config_hooks),
      config_lifecycle_hooks_(config_lifecycle_hooks),
      status_hooks_(status_hooks),
      mqtt_hooks_(mqtt_hooks),
      device_runtime_hooks_(device_runtime_hooks)
{
    bluetooth_config_ = meshtastic_Config_BluetoothConfig_init_zero;
    bluetooth_config_.enabled = app_.isBleEnabled();
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
    initDefaultModuleConfig(&module_config_, app_.getSelfNodeId());
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
    from_radio_id_ = 0;
    setPhoneApiPhase(PhoneApiPhase::SendNothing, "reset");
    deferred_config_save_pending_ = false;
    deferred_module_config_save_pending_ = false;
    deferred_bluetooth_config_apply_pending_ = false;
    admin_edit_transaction_open_ = false;
    admin_edit_transaction_dirty_ = false;
    admin_edit_transaction_module_dirty_ = false;
    admin_edit_transaction_bluetooth_dirty_ = false;
    admin_edit_transaction_restart_pending_ = false;
    restart_pending_ = false;
    mqtt_proxy_deferral_count_ = 0;
    output_queue_.clear();
    output_event_scratch_.clear();
    node_projection_cache_ = {};
    node_projection_cache_next_ = 0;
}

void MeshtasticPhoneCore::onIncomingText(const chat::MeshIncomingText& msg)
{
    enqueueKnownNodeInfoProjection(msg.from);

    output_event_scratch_.clear();
    auto& packet = output_event_scratch_.payload.packet;
    fillPacketFromText(msg, &packet);
    logDual("[BLE][mtcore] enqueue text packet id=%08lX from=%08lX to=%08lX len=%u\n",
            static_cast<unsigned long>(packet.id),
            static_cast<unsigned long>(packet.from),
            static_cast<unsigned long>(packet.to),
            static_cast<unsigned>(packet.decoded.payload.size));
    output_event_scratch_.kind = OutputEventKind::Packet;
    output_event_scratch_.priority = OutputPriority::P1;
    output_event_scratch_.notify_id = packet.id;
    output_event_scratch_.coalesce_key = 0;
    (void)enqueueOutputEvent(output_event_scratch_, "text");
}

void MeshtasticPhoneCore::onIncomingData(const chat::MeshIncomingData& msg)
{
    if (!enqueueMetadataNodeInfoProjection(msg))
    {
        enqueueKnownNodeInfoProjection(msg.from);
    }

    output_event_scratch_.clear();
    auto& packet = output_event_scratch_.payload.packet;
    fillPacketFromData(msg, &packet);
    const OutputPriority priority = priorityForPacket(packet);
    logDual("[BLE][mtcore] enqueue data packet id=%08lX port=%u req=%08lX from=%08lX to=%08lX len=%u\n",
            static_cast<unsigned long>(packet.id),
            static_cast<unsigned>(packet.decoded.portnum),
            static_cast<unsigned long>(packet.decoded.request_id),
            static_cast<unsigned long>(packet.from),
            static_cast<unsigned long>(packet.to),
            static_cast<unsigned>(packet.decoded.payload.size));
    output_event_scratch_.kind = OutputEventKind::Packet;
    output_event_scratch_.priority = priority;
    output_event_scratch_.notify_id = packet.id;
    output_event_scratch_.coalesce_key = coalesceKeyForPacket(packet, priority);
    (void)enqueueOutputEvent(output_event_scratch_, "data");
}

bool MeshtasticPhoneCore::isSendingPackets() const
{
    return phone_api_phase_ == PhoneApiPhase::SendPackets;
}

bool MeshtasticPhoneCore::isConfigFlowActive() const
{
    return phone_api_phase_ == PhoneApiPhase::ConfigFlow;
}

void MeshtasticPhoneCore::debugLogMemoryLayout(const char* stage) const
{
    logDual("[BLE][mtcore][mem] stage=%s core=%p size=%u output_q=%p output_scratch=%p\n",
            stage ? stage : "unknown",
            static_cast<const void*>(this),
            static_cast<unsigned>(sizeof(*this)),
            static_cast<const void*>(&output_queue_),
            static_cast<const void*>(&output_event_scratch_));
    logDual("[BLE][mtcore][mem] cache=%p to=%p admin_req=%p admin_resp=%p reply=%p\n",
            static_cast<const void*>(node_projection_cache_.data()),
            static_cast<const void*>(&to_radio_scratch_),
            static_cast<const void*>(&admin_req_scratch_),
            static_cast<const void*>(&admin_resp_scratch_),
            static_cast<const void*>(&reply_packet_scratch_));
    logDual("[BLE][mtcore][mem] mqtt=%p metadata=%p from=%p module=%p bluetooth=%p\n",
            static_cast<const void*>(&mqtt_proxy_scratch_),
            static_cast<const void*>(&node_metadata_decode_scratch_),
            static_cast<const void*>(&from_radio_scratch_),
            static_cast<const void*>(&module_config_),
            static_cast<const void*>(&bluetooth_config_));
}

MeshtasticPhoneCore::PhoneApiPhase MeshtasticPhoneCore::phoneApiPhase() const
{
    return phone_api_phase_;
}

bool MeshtasticPhoneCore::canHandleMqttProxy() const
{
    return true;
}

bool MeshtasticPhoneCore::canEmitSteadyStateFrame() const
{
    return isSendingPackets();
}

bool MeshtasticPhoneCore::hasDeferredSideEffects() const
{
    return deferred_config_save_pending_ ||
           deferred_module_config_save_pending_ ||
           deferred_bluetooth_config_apply_pending_ ||
           restart_pending_;
}

bool MeshtasticPhoneCore::shouldEmitMqttProxyBeforeOutput() const
{
    if (!mqtt_hooks_ || !mqtt_hooks_->hasMqttProxyToPhone() || hasDeferredSideEffects())
    {
        return false;
    }

    const OutputEvent* next = output_queue_.peek();
    if (!next)
    {
        return false;
    }
    if (next->priority == OutputPriority::P0 || next->priority == OutputPriority::P1)
    {
        return false;
    }
    if (next->priority == OutputPriority::P3)
    {
        return true;
    }
    return mqtt_proxy_deferral_count_ >= kMqttProxyMaxP2Deferrals;
}

void MeshtasticPhoneCore::recordMqttProxyDeferral(const OutputEvent& event)
{
    if (!mqtt_hooks_ || !mqtt_hooks_->hasMqttProxyToPhone())
    {
        mqtt_proxy_deferral_count_ = 0;
        return;
    }
    if (event.priority == OutputPriority::P2 || event.priority == OutputPriority::P3)
    {
        if (mqtt_proxy_deferral_count_ < kMqttProxyMaxP2Deferrals)
        {
            ++mqtt_proxy_deferral_count_;
        }
    }
}

bool MeshtasticPhoneCore::popMqttProxyFrame(MeshtasticBleFrame* out)
{
    if (!out || !mqtt_hooks_ || !canEmitSteadyStateFrame())
    {
        return false;
    }

    auto& mqtt = mqtt_proxy_scratch_;
    std::memset(&mqtt, 0, sizeof(mqtt));
    if (!mqtt_hooks_->pollMqttProxyToPhone(&mqtt))
    {
        mqtt_proxy_deferral_count_ = 0;
        return false;
    }

    auto& from = from_radio_scratch_;
    std::memset(&from, 0, sizeof(from));
    from.which_payload_variant = meshtastic_FromRadio_mqttClientProxyMessage_tag;
    from.mqttClientProxyMessage = mqtt;
    logDual("[BLE][mtcore][mqtt] pop topic=%s retained=%u variant=%u len=%u deferrals=%u\n",
            mqtt.topic,
            mqtt.retained ? 1U : 0U,
            static_cast<unsigned>(mqtt.which_payload_variant),
            mqtt.which_payload_variant == meshtastic_MqttClientProxyMessage_data_tag
                ? static_cast<unsigned>(mqtt.payload_variant.data.size)
                : 0U,
            static_cast<unsigned>(mqtt_proxy_deferral_count_));
    mqtt_proxy_deferral_count_ = 0;
    return encodeFromRadio(from, 0, out, MeshtasticBleFrameKind::MqttProxy, MeshtasticBleFramePriority::P3);
}

void MeshtasticPhoneCore::setPhoneApiPhase(PhoneApiPhase phase, const char* reason)
{
    if (phone_api_phase_ == phase)
    {
        return;
    }

    phone_api_phase_ = phase;
    logDual("[BLE][mtcore][phase] %s reason=%s\n",
            phoneApiPhaseName(phase),
            reason ? reason : "unknown");
}

bool MeshtasticPhoneCore::shouldProjectNodeInfo(chat::NodeId node_id, uint32_t signature)
{
    if (node_id == 0 || signature == 0)
    {
        return false;
    }

    for (const auto& cached : node_projection_cache_)
    {
        if (cached.node_id == node_id && cached.signature == signature)
        {
            return false;
        }
    }

    auto& cache_entry = node_projection_cache_[node_projection_cache_next_];
    cache_entry.node_id = node_id;
    cache_entry.signature = signature;
    node_projection_cache_next_ = (node_projection_cache_next_ + 1U) % node_projection_cache_.size();
    return true;
}

bool MeshtasticPhoneCore::enqueueOutputEvent(const OutputEvent& event, const char* reason)
{
    OutputPushReport report{};
    if (!output_queue_.push(event, &report))
    {
        logDual("[BLE][mtcore][q] drop new kind=%u pri=%u reason=%s notify=%08lX depth=%u\n",
                static_cast<unsigned>(event.kind),
                static_cast<unsigned>(event.priority),
                reason ? reason : "?",
                static_cast<unsigned long>(event.notify_id),
                static_cast<unsigned>(output_queue_.size()));
        return false;
    }

    switch (report.result)
    {
    case OutputPushReport::Result::Replaced:
        logDual("[BLE][mtcore][q] replace kind=%u pri=%u reason=%s notify=%08lX depth=%u\n",
                static_cast<unsigned>(event.kind),
                static_cast<unsigned>(event.priority),
                reason ? reason : "?",
                static_cast<unsigned long>(event.notify_id),
                static_cast<unsigned>(output_queue_.size()));
        break;
    case OutputPushReport::Result::DroppedExisting:
        logDual("[BLE][mtcore][q] drop existing kind=%u pri=%u for kind=%u pri=%u reason=%s depth=%u\n",
                static_cast<unsigned>(report.dropped_kind),
                static_cast<unsigned>(report.dropped_priority),
                static_cast<unsigned>(event.kind),
                static_cast<unsigned>(event.priority),
                reason ? reason : "?",
                static_cast<unsigned>(output_queue_.size()));
        break;
    case OutputPushReport::Result::Enqueued:
    case OutputPushReport::Result::DroppedNew:
        break;
    }

    if (const OutputEvent* next = output_queue_.peek())
    {
        notifyFromNum(next->notify_id);
    }
    return true;
}

MeshtasticPhoneCore::OutputPriority MeshtasticPhoneCore::priorityForPacket(const meshtastic_MeshPacket& packet) const
{
    if (packet.which_payload_variant != meshtastic_MeshPacket_decoded_tag)
    {
        return OutputPriority::P2;
    }

    const auto portnum = static_cast<meshtastic_PortNum>(packet.decoded.portnum);
    if (portnum == meshtastic_PortNum_ADMIN_APP || portnum == meshtastic_PortNum_ROUTING_APP)
    {
        return OutputPriority::P0;
    }
    if (isPhoneTextPort(portnum))
    {
        return OutputPriority::P1;
    }
    if (packet.to == app_.getSelfNodeId() && packet.to != 0)
    {
        return OutputPriority::P1;
    }
    if (isLowPriorityStatePort(portnum))
    {
        return OutputPriority::P2;
    }
    return OutputPriority::P2;
}

uint32_t MeshtasticPhoneCore::coalesceKeyForPacket(const meshtastic_MeshPacket& packet,
                                                   OutputPriority priority) const
{
    if (priority != OutputPriority::P2 || packet.which_payload_variant != meshtastic_MeshPacket_decoded_tag)
    {
        return 0;
    }

    const auto portnum = static_cast<meshtastic_PortNum>(packet.decoded.portnum);
    if (!isLowPriorityStatePort(portnum))
    {
        return 0;
    }
    return outputCoalesceKey(static_cast<uint32_t>(portnum), packet.from, packet.to);
}

MeshtasticBleFramePriority MeshtasticPhoneCore::framePriorityForOutput(OutputPriority priority)
{
    switch (priority)
    {
    case OutputPriority::P0:
        return MeshtasticBleFramePriority::P0;
    case OutputPriority::P1:
        return MeshtasticBleFramePriority::P1;
    case OutputPriority::P2:
        return MeshtasticBleFramePriority::P2;
    case OutputPriority::P3:
    default:
        return MeshtasticBleFramePriority::P3;
    }
}

MeshtasticBleFrameKind MeshtasticPhoneCore::frameKindForPacket(const meshtastic_MeshPacket& packet)
{
    if (packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
        static_cast<meshtastic_PortNum>(packet.decoded.portnum) == meshtastic_PortNum_ADMIN_APP)
    {
        return MeshtasticBleFrameKind::AdminResponse;
    }
    return MeshtasticBleFrameKind::Packet;
}

void MeshtasticPhoneCore::enqueueKnownNodeInfoProjection(chat::NodeId node_id)
{
    if (node_id == 0 || node_id == app_.getSelfNodeId())
    {
        return;
    }

    PhoneNodeView entry{};
    if (!app_.findPhoneNode(node_id, entry) || !hasAndroidVisibleNodeName(entry))
    {
        return;
    }

    const uint32_t signature = nodeProjectionSignature(entry);
    if (!shouldProjectNodeInfo(entry.node_id, signature))
    {
        return;
    }

    output_event_scratch_.clear();
    output_event_scratch_.kind = OutputEventKind::NodeInfo;
    output_event_scratch_.priority = OutputPriority::P1;
    output_event_scratch_.notify_id = entry.node_id;
    output_event_scratch_.coalesce_key = outputCoalesceKey(0x4E494E46UL, entry.node_id, 0);
    fillNodeInfoFromEntry(entry, &output_event_scratch_.payload.node_info);
    logDual("[BLE][mtcore] enqueue node_info projection node=%08lX short=%s long=%s via_mqtt=%u\n",
            static_cast<unsigned long>(entry.node_id),
            entry.short_name,
            entry.long_name,
            entry.via_mqtt ? 1U : 0U);
    (void)enqueueOutputEvent(output_event_scratch_, "node_info_projection");
}

bool MeshtasticPhoneCore::enqueueMetadataNodeInfoProjection(const chat::MeshIncomingData& msg)
{
    if (msg.from == 0 || msg.from == app_.getSelfNodeId() ||
        !chat::meshtastic::isNodeMetadataPayload(static_cast<meshtastic_PortNum>(msg.portnum)) ||
        msg.payload.empty())
    {
        return false;
    }

    auto& data = node_metadata_decode_scratch_;
    data = meshtastic_Data_init_zero;
    if (msg.payload.size() > sizeof(data.payload.bytes))
    {
        return false;
    }
    data.portnum = static_cast<meshtastic_PortNum>(msg.portnum);
    data.dest = msg.to;
    data.source = msg.from;
    data.request_id = msg.request_id;
    data.want_response = msg.want_response;
    data.has_bitfield = true;
    data.bitfield = 0;
    data.payload.size = static_cast<pb_size_t>(msg.payload.size());
    if (data.payload.size > 0)
    {
        std::memcpy(data.payload.bytes, msg.payload.data(), data.payload.size);
    }

    chat::meshtastic::NodePayloadDecodeContext context{};
    context.fallback_node_id = msg.from;
    context.snr = msg.rx_meta.snr_db_x10 / 10.0f;
    context.rssi = msg.rx_meta.rssi_dbm_x10 / 10.0f;
    context.timestamp = msg.rx_meta.rx_timestamp_s;
    context.hops_away = msg.rx_meta.hop_count;
    context.channel = channelIndexFromId(msg.channel);
    context.via_mqtt = msg.rx_meta.from_is;

    chat::meshtastic::DecodedNodePayload node{};
    if (!chat::meshtastic::decodeNodeMetadataPayload(data, context, &node) ||
        node.node_id == 0 || node.node_id == app_.getSelfNodeId() ||
        (node.short_name.empty() && node.long_name.empty()))
    {
        return false;
    }

    const uint32_t signature = nodeProjectionSignature(node);
    if (!shouldProjectNodeInfo(node.node_id, signature))
    {
        return false;
    }

    output_event_scratch_.clear();
    output_event_scratch_.kind = OutputEventKind::NodeInfo;
    output_event_scratch_.priority = OutputPriority::P1;
    output_event_scratch_.notify_id = node.node_id;
    output_event_scratch_.coalesce_key = outputCoalesceKey(0x4E494E46UL, node.node_id, 0);
    fillNodeInfoFromDecodedPayload(node, &output_event_scratch_.payload.node_info);
    logDual("[BLE][mtcore] enqueue metadata node_info node=%08lX short=%s long=%s via_mqtt=%u port=%u\n",
            static_cast<unsigned long>(node.node_id),
            node.short_name.c_str(),
            node.long_name.c_str(),
            node.via_mqtt ? 1U : 0U,
            static_cast<unsigned>(msg.portnum));
    (void)enqueueOutputEvent(output_event_scratch_, "metadata_node_info");
    return true;
}

bool MeshtasticPhoneCore::handleToRadio(const uint8_t* data, size_t len)
{
    if (!data || len == 0 || len > meshtastic_ToRadio_size)
    {
        return false;
    }

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
        if (!canEmitSteadyStateFrame())
        {
            logDual("[BLE][mtcore][flow] packet ignore reason=not-send-packets phase=%s\n",
                    phoneApiPhaseName(phone_api_phase_));
            return false;
        }
        return handleToRadioPacket(to_radio.packet);
    case meshtastic_ToRadio_mqttClientProxyMessage_tag:
        if (!canHandleMqttProxy())
        {
            logDual("[BLE][mtcore][mqtt] skip reason=not-send-packets dir=to_radio phase=%s\n",
                    phoneApiPhaseName(phone_api_phase_));
            return false;
        }
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
        transport_.requestPhoneDisconnect();
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

    packet.from = app_.getSelfNodeId();
    packet.rx_time = nowSeconds();
    logDual("[BLE][mtcore] packet port=%u to=%08lX want_resp=%u len=%u\n",
            static_cast<unsigned>(packet.decoded.portnum),
            static_cast<unsigned long>(packet.to),
            packet.decoded.want_response ? 1U : 0U,
            static_cast<unsigned>(packet.decoded.payload.size));

    const bool admin_for_self =
        (packet.decoded.portnum == meshtastic_PortNum_ADMIN_APP) &&
        (packet.to == 0 || packet.to == app_.getSelfNodeId());
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
        chat::MessageId msg_id = 0;
        app_.sendPhoneText(channel, text, packet.id, packet.to, msg_id);
        enqueueQueueStatus(packet.id, msg_id != 0);
        return msg_id != 0;
    }

    const bool ok = app_.sendPhoneAppData(channel,
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
    auto cfg = app_.getMeshtasticPhoneConfig();

    switch (req.which_payload_variant)
    {
    case meshtastic_AdminMessage_get_owner_request_tag:
        resp.which_payload_variant = meshtastic_AdminMessage_get_owner_response_tag;
        resp.get_owner_response = buildSelfNodeInfo().user;
        has_resp = true;
        break;
    case meshtastic_AdminMessage_get_channel_request_tag:
    {
        const uint32_t raw_channel_request = req.get_channel_request;
        const uint8_t channel_index =
            static_cast<uint8_t>(raw_channel_request > 0 ? (raw_channel_request - 1) : 0);
        logDual("[BLE][mtcore][channel] get_req raw=%lu idx=%u\n",
                static_cast<unsigned long>(raw_channel_request),
                static_cast<unsigned>(channel_index));
        resp.which_payload_variant = meshtastic_AdminMessage_get_channel_response_tag;
        resp.get_channel_response = buildChannel(channel_index);
        logChannelSummary("get_resp", resp.get_channel_response);
        has_resp = true;
        break;
    }
    case meshtastic_AdminMessage_get_config_request_tag:
        logDual("[BLE][mtcore] get_config req type=%u\n",
                static_cast<unsigned>(req.get_config_request));
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
        app_.setMeshtasticPhoneConfig(cfg);
        if (admin_edit_transaction_open_)
        {
            admin_edit_transaction_dirty_ = true;
            logDual("[BLE][mtcore] set_owner save deferred by edit transaction\n");
        }
        else
        {
            deferred_config_save_pending_ = true;
        }
        app_.applyUserInfo();
        resp.which_payload_variant = meshtastic_AdminMessage_get_owner_response_tag;
        resp.get_owner_response = buildSelfNodeInfo().user;
        has_resp = true;
        break;
    case meshtastic_AdminMessage_set_channel_tag:
    {
        logChannelSummary("set_req", req.set_channel);
        const bool has_module_settings =
            req.set_channel.has_settings && req.set_channel.settings.has_module_settings;
        auto apply_module_settings = [&](bool& dst_has_module_settings,
                                         uint32_t& dst_position_precision,
                                         bool& dst_is_muted)
        {
            dst_has_module_settings = has_module_settings;
            if (has_module_settings)
            {
                dst_position_precision = req.set_channel.settings.module_settings.position_precision;
                dst_is_muted = req.set_channel.settings.module_settings.is_muted;
            }
            else
            {
                dst_position_precision = 0;
                dst_is_muted = false;
            }
        };

        if (req.set_channel.index == 0)
        {
            cfg.primary_enabled = (req.set_channel.role != meshtastic_Channel_Role_DISABLED);
            cfg.primary_uplink_enabled = req.set_channel.settings.uplink_enabled;
            cfg.primary_downlink_enabled = req.set_channel.settings.downlink_enabled;
            copyBounded(cfg.mesh.primary_channel_name,
                        sizeof(cfg.mesh.primary_channel_name),
                        req.set_channel.settings.name);
            cfg.mesh.primary_channel_id = req.set_channel.settings.id;
            applyChannelPsk(cfg.mesh.primary_key,
                            sizeof(cfg.mesh.primary_key),
                            &cfg.mesh.primary_key_len,
                            req.set_channel.settings.psk);
            apply_module_settings(cfg.primary_channel_has_module_settings,
                                  cfg.primary_channel_position_precision,
                                  cfg.primary_channel_is_muted);
        }
        else if (req.set_channel.index == 1)
        {
            cfg.secondary_enabled = (req.set_channel.role != meshtastic_Channel_Role_DISABLED);
            cfg.secondary_uplink_enabled = req.set_channel.settings.uplink_enabled;
            cfg.secondary_downlink_enabled = req.set_channel.settings.downlink_enabled;
            copyBounded(cfg.mesh.secondary_channel_name,
                        sizeof(cfg.mesh.secondary_channel_name),
                        req.set_channel.settings.name);
            cfg.mesh.secondary_channel_id = req.set_channel.settings.id;
            applyChannelPsk(cfg.mesh.secondary_key,
                            sizeof(cfg.mesh.secondary_key),
                            &cfg.mesh.secondary_key_len,
                            req.set_channel.settings.psk);
            apply_module_settings(cfg.secondary_channel_has_module_settings,
                                  cfg.secondary_channel_position_precision,
                                  cfg.secondary_channel_is_muted);
        }
        app_.setMeshtasticPhoneConfig(cfg);
        app_.applyMeshConfig();
        if (admin_edit_transaction_open_)
        {
            admin_edit_transaction_dirty_ = true;
            logDual("[BLE][mtcore] set_channel save deferred by edit transaction\n");
        }
        else
        {
            deferred_config_save_pending_ = true;
        }
        resp.which_payload_variant = meshtastic_AdminMessage_get_channel_response_tag;
        resp.get_channel_response = buildChannel(req.set_channel.index);
        logChannelSummary("set_resp", resp.get_channel_response);
        has_resp = true;
        break;
    }
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
            app_.setMeshtasticPhoneConfig(cfg);
            if (admin_edit_transaction_open_)
            {
                admin_edit_transaction_dirty_ = true;
                logDual("[BLE][mtcore] set_config lora save deferred by edit transaction\n");
            }
            else
            {
                deferred_config_save_pending_ = true;
                logDual("[BLE][mtcore] set_config lora save deferred\n");
            }
            app_.applyMeshConfig();
            logDual("[BLE][mtcore] set_config lora post-apply\n");
            break;
        case meshtastic_Config_position_tag:
            cfg.gps_enabled = req.set_config.payload_variant.position.gps_enabled;
            cfg.gps_interval_ms = req.set_config.payload_variant.position.gps_update_interval * 1000U;
            app_.setMeshtasticPhoneConfig(cfg);
            if (admin_edit_transaction_open_)
            {
                admin_edit_transaction_dirty_ = true;
                logDual("[BLE][mtcore] set_config position save deferred by edit transaction\n");
            }
            else
            {
                deferred_config_save_pending_ = true;
            }
            app_.applyPositionConfig();
            break;
        case meshtastic_Config_bluetooth_tag:
            bluetooth_config_ = req.set_config.payload_variant.bluetooth;
            bluetooth_config_.enabled = req.set_config.payload_variant.bluetooth.enabled;
            if (bluetooth_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN)
            {
                bluetooth_config_.fixed_pin = 0;
            }
            if (admin_edit_transaction_open_)
            {
                admin_edit_transaction_bluetooth_dirty_ = true;
                logDual("[BLE][mtcore] set_config bluetooth apply deferred by edit transaction enabled=%u\n",
                        bluetooth_config_.enabled ? 1U : 0U);
            }
            else
            {
                deferred_bluetooth_config_apply_pending_ = true;
                logDual("[BLE][mtcore] set_config bluetooth apply deferred enabled=%u\n",
                        bluetooth_config_.enabled ? 1U : 0U);
            }
            break;
        case meshtastic_Config_device_tag:
        {
            const char* tzdef = req.set_config.payload_variant.device.tzdef;
            if (tzdef[0] != '\0')
            {
                int offset_min = 0;
                if (parsePosixTzOffsetMinutes(tzdef, &offset_min))
                {
                    const auto* profile = ::platform::ui::time::timezone_profile_by_tzdef(tzdef);
                    if (device_runtime_hooks_)
                    {
                        if (profile)
                        {
                            device_runtime_hooks_->setTimezoneProfileId(profile->id);
                        }
                        else
                        {
                            device_runtime_hooks_->setTimezoneOffsetMinutes(offset_min);
                        }
                    }
                    saveStoredTimezoneTzdef(device_runtime_hooks_, tzdef);
                    logDual("[BLE][mtcore] set_config device tzdef=%s offset_min=%d profile=%d\n",
                            tzdef,
                            offset_min,
                            profile ? profile->id : -1);
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
            // MQTT bridge settings are applied by module_config_hooks_ after save.
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
        if (admin_edit_transaction_open_)
        {
            admin_edit_transaction_module_dirty_ = true;
            logDual("[BLE][mtcore] set_module_config save deferred by edit transaction variant=%u\n",
                    static_cast<unsigned>(req.set_module_config.which_payload_variant));
        }
        else
        {
            deferred_module_config_save_pending_ = true;
            logDual("[BLE][mtcore] set_module_config save deferred variant=%u\n",
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
    case meshtastic_AdminMessage_begin_edit_settings_tag:
        admin_edit_transaction_open_ = true;
        admin_edit_transaction_dirty_ = false;
        admin_edit_transaction_module_dirty_ = false;
        admin_edit_transaction_bluetooth_dirty_ = false;
        admin_edit_transaction_restart_pending_ = false;
        logDual("[BLE][mtcore] edit settings begin\n");
        break;
    case meshtastic_AdminMessage_commit_edit_settings_tag:
    {
        const bool dirty = admin_edit_transaction_dirty_;
        const bool module_dirty = admin_edit_transaction_module_dirty_;
        const bool bluetooth_dirty = admin_edit_transaction_bluetooth_dirty_;
        const bool restart_after_commit = admin_edit_transaction_restart_pending_;
        admin_edit_transaction_open_ = false;
        admin_edit_transaction_dirty_ = false;
        admin_edit_transaction_module_dirty_ = false;
        admin_edit_transaction_bluetooth_dirty_ = false;
        admin_edit_transaction_restart_pending_ = false;
        if (dirty)
        {
            deferred_config_save_pending_ = true;
        }
        if (module_dirty)
        {
            deferred_module_config_save_pending_ = true;
        }
        if (bluetooth_dirty)
        {
            deferred_bluetooth_config_apply_pending_ = true;
        }
        if (restart_after_commit)
        {
            restart_pending_ = true;
        }
        logDual("[BLE][mtcore] edit settings commit dirty=%u module_dirty=%u bluetooth_dirty=%u restart=%u\n",
                dirty ? 1U : 0U,
                module_dirty ? 1U : 0U,
                bluetooth_dirty ? 1U : 0U,
                restart_after_commit ? 1U : 0U);
        break;
    }
    case meshtastic_AdminMessage_set_time_only_tag:
    {
        return app_.syncCurrentEpochSeconds(static_cast<uint32_t>(req.set_time_only));
    }
    case meshtastic_AdminMessage_remove_by_nodenum_tag:
        return true;
    case meshtastic_AdminMessage_factory_reset_config_tag:
        app_.resetMeshConfig();
        app_.clearNodeDb();
        app_.clearMessageDb();
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
    reply.from = app_.getSelfNodeId();
    reply.to = app_.getSelfNodeId();
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
    output_event_scratch_.clear();
    output_event_scratch_.kind = OutputEventKind::Packet;
    output_event_scratch_.priority = OutputPriority::P0;
    output_event_scratch_.notify_id = reply.id;
    output_event_scratch_.payload.packet = reply;
    (void)enqueueOutputEvent(output_event_scratch_, "admin_response");
    return true;
}

bool MeshtasticPhoneCore::handleLocalSelfPacket(meshtastic_MeshPacket& packet)
{
    const uint32_t self = app_.getSelfNodeId();
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
        output_event_scratch_.clear();
        output_event_scratch_.kind = OutputEventKind::Packet;
        output_event_scratch_.priority = OutputPriority::P1;
        output_event_scratch_.notify_id = reply.id;
        output_event_scratch_.payload.packet = reply;
        (void)enqueueOutputEvent(output_event_scratch_, "telemetry_loopback");
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
        output_event_scratch_.clear();
        output_event_scratch_.kind = OutputEventKind::Packet;
        output_event_scratch_.priority = OutputPriority::P1;
        output_event_scratch_.notify_id = reply.id;
        output_event_scratch_.payload.packet = reply;
        (void)enqueueOutputEvent(output_event_scratch_, "position_loopback");
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
        output_event_scratch_.clear();
        output_event_scratch_.kind = OutputEventKind::Packet;
        output_event_scratch_.priority = OutputPriority::P1;
        output_event_scratch_.notify_id = reply.id;
        output_event_scratch_.payload.packet = reply;
        (void)enqueueOutputEvent(output_event_scratch_, "nodeinfo_loopback");
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
    for (uint8_t count = 0; count < kPhoneOutputQueueDepth; ++count)
    {
        chat::MeshIncomingData incoming{};
        if (!app_.pollIncomingPhoneData(incoming))
        {
            break;
        }
        output_event_scratch_.clear();
        auto& packet = output_event_scratch_.payload.packet;
        fillPacketFromData(incoming, &packet);
        const OutputPriority priority = priorityForPacket(packet);
        output_event_scratch_.kind = OutputEventKind::Packet;
        output_event_scratch_.priority = priority;
        output_event_scratch_.notify_id = packet.id;
        output_event_scratch_.coalesce_key = coalesceKeyForPacket(packet, priority);
        (void)enqueueOutputEvent(output_event_scratch_, "app_data");
    }
}

bool MeshtasticPhoneCore::popToPhone(MeshtasticBleFrame* out)
{
    if (!out)
    {
        return false;
    }

    if (phone_api_phase_ == PhoneApiPhase::ConfigFlow && popConfigSnapshotFrame(out))
    {
        return true;
    }

    if (!canEmitSteadyStateFrame())
    {
        if (canEmitSendNothingLivenessFrame())
        {
            auto& from = from_radio_scratch_;
            std::memset(&from, 0, sizeof(from));
            output_event_scratch_.clear();
            if (output_queue_.pop(&output_event_scratch_) &&
                output_event_scratch_.kind == OutputEventKind::QueueStatus)
            {
                from.which_payload_variant = meshtastic_FromRadio_queueStatus_tag;
                from.queueStatus = output_event_scratch_.payload.queue_status;
                return encodeFromRadio(from,
                                       output_event_scratch_.notify_id,
                                       out,
                                       MeshtasticBleFrameKind::Liveness,
                                       MeshtasticBleFramePriority::P0);
            }
        }
        return false;
    }

    auto& from = from_radio_scratch_;
    std::memset(&from, 0, sizeof(from));
    output_event_scratch_.clear();
    if (shouldEmitMqttProxyBeforeOutput() && popMqttProxyFrame(out))
    {
        return true;
    }

    if (output_queue_.pop(&output_event_scratch_))
    {
        switch (output_event_scratch_.kind)
        {
        case OutputEventKind::QueueStatus:
            from.which_payload_variant = meshtastic_FromRadio_queueStatus_tag;
            from.queueStatus = output_event_scratch_.payload.queue_status;
            recordMqttProxyDeferral(output_event_scratch_);
            return encodeFromRadio(from,
                                   output_event_scratch_.notify_id,
                                   out,
                                   MeshtasticBleFrameKind::QueueStatus,
                                   MeshtasticBleFramePriority::P0);
        case OutputEventKind::NodeInfo:
            from.which_payload_variant = meshtastic_FromRadio_node_info_tag;
            from.node_info = output_event_scratch_.payload.node_info;
            recordMqttProxyDeferral(output_event_scratch_);
            return encodeFromRadio(from,
                                   output_event_scratch_.notify_id,
                                   out,
                                   MeshtasticBleFrameKind::NodeInfo,
                                   framePriorityForOutput(output_event_scratch_.priority));
        case OutputEventKind::Packet:
            from.which_payload_variant = meshtastic_FromRadio_packet_tag;
            from.packet = output_event_scratch_.payload.packet;
            recordMqttProxyDeferral(output_event_scratch_);
            return encodeFromRadio(from,
                                   output_event_scratch_.notify_id,
                                   out,
                                   frameKindForPacket(output_event_scratch_.payload.packet),
                                   framePriorityForOutput(output_event_scratch_.priority));
        default:
            break;
        }
    }

    if (deferred_config_save_pending_)
    {
        deferred_config_save_pending_ = false;
        logDual("[BLE][mtcore] deferred config save after response drain\n");
        app_.saveConfig();
    }

    if (deferred_module_config_save_pending_)
    {
        deferred_module_config_save_pending_ = false;
        if (module_config_hooks_)
        {
            logDual("[BLE][mtcore] deferred module config save after response drain\n");
            module_config_hooks_->saveModuleConfig(module_config_);
        }
    }

    if (deferred_bluetooth_config_apply_pending_)
    {
        deferred_bluetooth_config_apply_pending_ = false;
        if (bluetooth_config_hooks_)
        {
            logDual("[BLE][mtcore] deferred bluetooth config save after response drain\n");
            bluetooth_config_hooks_->saveBluetoothConfig(bluetooth_config_);
        }
        logDual("[BLE][mtcore] deferred bluetooth enabled apply=%u after response drain\n",
                bluetooth_config_.enabled ? 1U : 0U);
        app_.setBleEnabled(bluetooth_config_.enabled);
    }

    if (restart_pending_)
    {
        restart_pending_ = false;
        logDual("[BLE][mtcore] restart after module config save\n");
        app_.restartDevice();
        return false;
    }

    return popMqttProxyFrame(out);
}

bool MeshtasticPhoneCore::popConfigSnapshotFrame(MeshtasticBleFrame* out)
{
    if (!out || phone_api_phase_ != PhoneApiPhase::ConfigFlow)
    {
        return false;
    }

    auto& from = from_radio_scratch_;
    std::memset(&from, 0, sizeof(from));
    const uint32_t from_num = config_nonce_;
    const bool only_config = config_nonce_ == defaults::kConfigNonceOnlyConfig;
    const bool only_nodes = config_nonce_ == defaults::kConfigNonceOnlyNodes;

    if (only_nodes && config_node_index_ < 2)
    {
        config_node_index_ = 2;
    }

    if (!only_nodes && config_node_index_ == 0)
    {
        from.which_payload_variant = meshtastic_FromRadio_my_info_tag;
        fillMyInfo(&from.my_info);
        ++config_node_index_;
        logDual("[BLE][mtcore][cfg#%lu] frame my_info nonce=%08lX\n",
                static_cast<unsigned long>(config_request_seq_),
                static_cast<unsigned long>(from_num));
        logRuntimeFootprint("cfg_my_info");
        return encodeFromRadio(from, from_num, out, MeshtasticBleFrameKind::Config, MeshtasticBleFramePriority::P0);
    }

    if (!only_nodes && config_node_index_ == 1)
    {
        from.which_payload_variant = meshtastic_FromRadio_deviceuiConfig_tag;
        fillDeviceUi(&from.deviceuiConfig);
        ++config_node_index_;
        logDual("[BLE][mtcore][cfg#%lu] frame deviceui nonce=%08lX\n",
                static_cast<unsigned long>(config_request_seq_),
                static_cast<unsigned long>(from_num));
        logRuntimeFootprint("cfg_deviceui");
        return encodeFromRadio(from, from_num, out, MeshtasticBleFrameKind::Config, MeshtasticBleFramePriority::P0);
    }

    if (only_config && config_node_index_ == 2)
    {
        config_node_index_ = 3;
    }

    if (!only_config && config_node_index_ == 2)
    {
        from.which_payload_variant = meshtastic_FromRadio_node_info_tag;
        fillSelfNodeInfo(&from.node_info);
        ++config_node_index_;
        logDual("[BLE][mtcore][cfg#%lu] frame self_node nonce=%08lX\n",
                static_cast<unsigned long>(config_request_seq_),
                static_cast<unsigned long>(from_num));
        logRuntimeFootprint("cfg_self_node");
        return encodeFromRadio(from, from_num, out, MeshtasticBleFrameKind::Config, MeshtasticBleFramePriority::P0);
    }

    const size_t node_count = app_.phoneNodeCount();
    PhoneNodeView entry{};
    while (!only_config && (config_node_index_ - 3) < node_count)
    {
        const size_t node_index = config_node_index_ - 3;
        ++config_node_index_;
        if (!app_.getPhoneNodeByIndex(node_index, entry))
        {
            continue;
        }
        if (entry.node_id == 0 || entry.node_id == app_.getSelfNodeId())
        {
            continue;
        }
        if (!hasAndroidVisibleNodeName(entry))
        {
            continue;
        }
        from.which_payload_variant = meshtastic_FromRadio_node_info_tag;
        fillNodeInfoFromEntry(entry, &from.node_info);
        return encodeFromRadio(from, entry.node_id, out, MeshtasticBleFrameKind::Config, MeshtasticBleFramePriority::P0);
    }

    if (!only_nodes && config_channel_index_ == 0)
    {
        from.which_payload_variant = meshtastic_FromRadio_metadata_tag;
        fillMetadata(&from.metadata);
        ++config_channel_index_;
        return encodeFromRadio(from, from_num, out, MeshtasticBleFrameKind::Config, MeshtasticBleFramePriority::P0);
    }

    const uint8_t channel_slot = static_cast<uint8_t>(config_channel_index_ - 1);
    if (!only_nodes && channel_slot < defaults::kMaxMeshtasticChannels)
    {
        from.which_payload_variant = meshtastic_FromRadio_channel_tag;
        fillChannel(channel_slot, &from.channel);
        logChannelSummary("snapshot", from.channel);
        ++config_channel_index_;
        return encodeFromRadio(from, from_num, out, MeshtasticBleFrameKind::Config, MeshtasticBleFramePriority::P0);
    }

    if (!only_nodes && config_type_index_ < (sizeof(kConfigSnapshotTypes) / sizeof(kConfigSnapshotTypes[0])))
    {
        from.which_payload_variant = meshtastic_FromRadio_config_tag;
        fillConfig(kConfigSnapshotTypes[config_type_index_++], &from.config);
        return encodeFromRadio(from, from_num, out, MeshtasticBleFrameKind::Config, MeshtasticBleFramePriority::P0);
    }

    if (!only_nodes && config_module_type_index_ < (sizeof(kModuleSnapshotTypes) / sizeof(kModuleSnapshotTypes[0])))
    {
        from.which_payload_variant = meshtastic_FromRadio_moduleConfig_tag;
        fillModuleConfig(kModuleSnapshotTypes[config_module_type_index_++], &from.moduleConfig);
        return encodeFromRadio(from, from_num, out, MeshtasticBleFrameKind::Config, MeshtasticBleFramePriority::P0);
    }

    from.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
    from.config_complete_id = config_nonce_;
    const size_t completed_node_index = config_node_index_;
    const uint8_t completed_channel_index = config_channel_index_;
    const uint8_t completed_config_index = config_type_index_;
    const uint8_t completed_module_index = config_module_type_index_;
    const PhoneApiPhase next_phase = (from_num == defaults::kConfigNonceOnlyConfig)
                                         ? PhoneApiPhase::SendNothing
                                         : PhoneApiPhase::SendPackets;
    setPhoneApiPhase(next_phase,
                     next_phase == PhoneApiPhase::SendPackets
                         ? "config_complete_send_packets"
                         : "config_complete_wait_next_stage");
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
    return encodeFromRadio(from, from_num, out, MeshtasticBleFrameKind::Config, MeshtasticBleFramePriority::P0);
}

bool MeshtasticPhoneCore::encodeFromRadio(meshtastic_FromRadio& from, uint32_t from_num, MeshtasticBleFrame* out,
                                          MeshtasticBleFrameKind kind, MeshtasticBleFramePriority priority)
{
    if (!out)
    {
        logDual("[BLE][mtcore] encode from_radio failed: out=null variant=%u from_num=%08lX kind=%u pri=%u\n",
                static_cast<unsigned>(from.which_payload_variant),
                static_cast<unsigned long>(from_num),
                static_cast<unsigned>(kind),
                static_cast<unsigned>(priority));
        return false;
    }

    out->len = 0;
    out->kind = MeshtasticBleFrameKind::None;
    out->priority = MeshtasticBleFramePriority::P3;
    out->from_num = 0;

    from.id = ++from_radio_id_;
    if (from.id == 0)
    {
        from.id = ++from_radio_id_;
    }
    pb_ostream_t ostream = pb_ostream_from_buffer(out->buf, sizeof(out->buf));
    if (!pb_encode(&ostream, meshtastic_FromRadio_fields, &from))
    {
        logDual("[BLE][mtcore] encode from_radio failed: variant=%u from_num=%08lX kind=%u pri=%u\n",
                static_cast<unsigned>(from.which_payload_variant),
                static_cast<unsigned long>(from_num),
                static_cast<unsigned>(kind),
                static_cast<unsigned>(priority));
        return false;
    }

    out->len = static_cast<uint16_t>(ostream.bytes_written);
    out->kind = kind;
    out->priority = priority;
    out->from_num = from_num;
    logDual("[BLE][mtcore] encode from_radio ok: variant=%u id=%08lX from_num=%08lX len=%u kind=%u pri=%u\n",
            static_cast<unsigned>(from.which_payload_variant),
            static_cast<unsigned long>(from.id),
            static_cast<unsigned long>(from_num),
            static_cast<unsigned>(out->len),
            static_cast<unsigned>(out->kind),
            static_cast<unsigned>(out->priority));
    return true;
}

void MeshtasticPhoneCore::enqueueQueueStatus(uint32_t packet_id, bool ok)
{
    output_event_scratch_.clear();
    auto& status = output_event_scratch_.payload.queue_status;
    status.res = ok ? 0 : 1;
    const size_t queue_depth = output_queue_.size();
    status.free = static_cast<uint32_t>(
        queue_depth < output_queue_.capacity() ? output_queue_.capacity() - queue_depth : 0U);
    status.maxlen = static_cast<uint32_t>(output_queue_.capacity());
    status.mesh_packet_id = packet_id;
    output_event_scratch_.kind = OutputEventKind::QueueStatus;
    output_event_scratch_.priority = OutputPriority::P0;
    output_event_scratch_.notify_id = packet_id;
    (void)enqueueOutputEvent(output_event_scratch_, "queue_status");
    logDual("[BLE][mtcore] queue status mesh_packet_id=%08lX ok=%u depth=%u\n",
            static_cast<unsigned long>(packet_id),
            ok ? 1U : 0U,
            static_cast<unsigned>(output_queue_.size()));
}

void MeshtasticPhoneCore::enqueueConfigSnapshot(uint32_t config_nonce)
{
    ++config_request_seq_;
    config_nonce_ = config_nonce;
    config_node_index_ = 0;
    config_channel_index_ = 0;
    config_type_index_ = 0;
    config_module_type_index_ = 0;
    setPhoneApiPhase(PhoneApiPhase::ConfigFlow, "want_config");
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

void MeshtasticPhoneCore::notifyFromNum(uint32_t from_num)
{
    if (phone_api_phase_ == PhoneApiPhase::SendNothing)
    {
        if (!canEmitSendNothingLivenessFrame())
        {
            logDual("[BLE][mtcore][flow] from_num defer phase=%s source=%08lX\n",
                    phoneApiPhaseName(phone_api_phase_),
                    static_cast<unsigned long>(from_num));
            return;
        }
    }
    transport_.notifyFromNum(from_num);
}

bool MeshtasticPhoneCore::canEmitSendNothingLivenessFrame() const
{
    if (phone_api_phase_ != PhoneApiPhase::SendNothing)
    {
        return false;
    }
    const OutputEvent* next = output_queue_.peek();
    return next && next->kind == OutputEventKind::QueueStatus;
}

void MeshtasticPhoneCore::fillMyInfo(meshtastic_MyNodeInfo* out) const
{
    if (!out)
    {
        return;
    }
    meshtastic_MyNodeInfo& info = *out;
    std::memset(&info, 0, sizeof(info));
    info.my_node_num = app_.getSelfNodeId();
    info.reboot_count = 0;
    info.min_app_version = defaults::kOfficialMinAppVersion;

    size_t nodedb_count = 1 + app_.phoneNodeCount();
    if (nodedb_count > 0xFFFFU)
    {
        nodedb_count = 0xFFFFU;
    }
    info.nodedb_count = static_cast<uint16_t>(nodedb_count);

    uint8_t mac[6] = {};
    const bool has_mac = app_.getDeviceMacAddress(mac);
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
    info.num = app_.getSelfNodeId();
    info.has_user = true;

    char long_name[32] = {};
    char short_name[16] = {};
    app_.getEffectiveUserInfo(long_name, sizeof(long_name), short_name, sizeof(short_name));

    char user_id[16] = {};
    std::snprintf(user_id, sizeof(user_id), "!%08lx", static_cast<unsigned long>(app_.getSelfNodeId()));
    copyBounded(info.user.id, sizeof(info.user.id), user_id);
    copyBounded(info.user.long_name, sizeof(info.user.long_name), long_name);
    copyBounded(info.user.short_name, sizeof(info.user.short_name), short_name);
    info.user.hw_model = localHardwareModel();
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

void MeshtasticPhoneCore::fillNodeInfoFromEntry(const PhoneNodeView& entry, meshtastic_NodeInfo* out) const
{
    if (!out)
    {
        return;
    }
    meshtastic_NodeInfo& info = *out;
    std::memset(&info, 0, sizeof(info));
    info.num = entry.node_id;
    info.has_user = hasAndroidVisibleNodeName(entry);

    char user_id[16] = {};
    if (info.has_user)
    {
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
    }
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

    if (entry.position.valid)
    {
        info.has_position = true;
        info.position = meshtastic_Position_init_zero;
        info.position.has_latitude_i = true;
        info.position.latitude_i = entry.position.latitude_i;
        info.position.has_longitude_i = true;
        info.position.longitude_i = entry.position.longitude_i;
        info.position.timestamp = entry.position.timestamp;
        info.position.has_altitude = entry.position.has_altitude;
        info.position.altitude = entry.position.altitude;
        info.position.precision_bits = entry.position.precision_bits;
        info.position.PDOP = entry.position.pdop;
        info.position.HDOP = entry.position.hdop;
        info.position.VDOP = entry.position.vdop;
        info.position.gps_accuracy = entry.position.gps_accuracy_mm;
    }
}

void MeshtasticPhoneCore::fillNodeInfoFromDecodedPayload(
    const chat::meshtastic::DecodedNodePayload& node, meshtastic_NodeInfo* out) const
{
    if (!out)
    {
        return;
    }

    meshtastic_NodeInfo& info = *out;
    std::memset(&info, 0, sizeof(info));
    info.num = node.node_id;
    info.has_user = node.has_user || !node.short_name.empty() || !node.long_name.empty();

    if (info.has_user)
    {
        char user_id[16] = {};
        std::snprintf(user_id, sizeof(user_id), "!%08lX", static_cast<unsigned long>(node.node_id));
        copyBounded(info.user.id, sizeof(info.user.id), user_id);
        copyBounded(info.user.long_name, sizeof(info.user.long_name), node.long_name.c_str());
        copyBounded(info.user.short_name, sizeof(info.user.short_name), node.short_name.c_str());
        if (node.has_macaddr)
        {
            std::memcpy(info.user.macaddr, node.macaddr.data(), sizeof(info.user.macaddr));
        }
        info.user.hw_model = static_cast<meshtastic_HardwareModel>(node.hw_model);
        info.user.role = roleFromEntry(node.role);
        if (node.has_public_key)
        {
            const size_t copy_len = std::min(node.public_key.size(), sizeof(info.user.public_key.bytes));
            std::memcpy(info.user.public_key.bytes, node.public_key.data(), copy_len);
            info.user.public_key.size = static_cast<pb_size_t>(copy_len);
        }
    }

    info.channel = node.channel;
    info.last_heard = node.timestamp;
    info.snr = node.snr;
    info.has_hops_away = node.hops_away != 0xFFU;
    info.hops_away = node.hops_away;
    info.via_mqtt = node.via_mqtt;
    info.is_ignored = node.is_ignored;
    info.is_key_manually_verified = node.key_manually_verified;
    if (node.has_device_metrics)
    {
        info.has_device_metrics = true;
        info.device_metrics.has_battery_level = node.device_metrics.has_battery_level;
        info.device_metrics.battery_level = node.device_metrics.battery_level;
        info.device_metrics.has_voltage = node.device_metrics.has_voltage;
        info.device_metrics.voltage = node.device_metrics.voltage;
        info.device_metrics.has_channel_utilization = node.device_metrics.has_channel_utilization;
        info.device_metrics.channel_utilization = node.device_metrics.channel_utilization;
        info.device_metrics.has_air_util_tx = node.device_metrics.has_air_util_tx;
        info.device_metrics.air_util_tx = node.device_metrics.air_util_tx;
        info.device_metrics.has_uptime_seconds = node.device_metrics.has_uptime_seconds;
        info.device_metrics.uptime_seconds = node.device_metrics.uptime_seconds;
    }

    if (node.has_position && node.position.valid)
    {
        info.has_position = true;
        info.position = meshtastic_Position_init_zero;
        info.position.has_latitude_i = true;
        info.position.latitude_i = node.position.latitude_i;
        info.position.has_longitude_i = true;
        info.position.longitude_i = node.position.longitude_i;
        info.position.timestamp = node.position.timestamp;
        info.position.has_altitude = node.position.has_altitude;
        info.position.altitude = node.position.altitude;
        info.position.precision_bits = node.position.precision_bits;
        info.position.PDOP = node.position.pdop;
        info.position.HDOP = node.position.hdop;
        info.position.VDOP = node.position.vdop;
        info.position.gps_accuracy = node.position.gps_accuracy_mm;
    }
}

meshtastic_NodeInfo MeshtasticPhoneCore::buildNodeInfoFromEntry(const PhoneNodeView& entry) const
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
    copyBounded(metadata.firmware_version, sizeof(metadata.firmware_version), defaults::kCompatFirmwareVersion);
    metadata.device_state_version = defaults::kOfficialDeviceStateVersion;
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
    metadata.hasPKC = app_.isMeshPkiReady();
#endif
    metadata.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    metadata.position_flags = 0;
    metadata.hw_model = localHardwareModel();
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
    const size_t total = app_.phoneNodeCount();
    stats.num_total_nodes = static_cast<uint16_t>(std::min<size_t>(total, 0xFFFFU));
    stats.num_online_nodes = stats.num_total_nodes;
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

    const auto cfg = app_.getMeshtasticPhoneConfig();
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
    channel.settings.uplink_enabled = (idx == 0) ? cfg.primary_uplink_enabled : cfg.secondary_uplink_enabled;
    channel.settings.downlink_enabled = (idx == 0) ? cfg.primary_downlink_enabled : cfg.secondary_downlink_enabled;
    channel.settings.has_module_settings =
        (idx == 0) ? cfg.primary_channel_has_module_settings : cfg.secondary_channel_has_module_settings;
    if (channel.settings.has_module_settings)
    {
        channel.settings.module_settings.position_precision =
            (idx == 0) ? cfg.primary_channel_position_precision : cfg.secondary_channel_position_precision;
        channel.settings.module_settings.is_muted =
            (idx == 0) ? cfg.primary_channel_is_muted : cfg.secondary_channel_is_muted;
    }

    if (idx == 0)
    {
        copyBounded(channel.settings.name,
                    sizeof(channel.settings.name),
                    chat::meshtastic::primaryChannelName(cfg.mesh));
        channel.settings.id = cfg.mesh.primary_channel_id;
        const size_t key_len = meshtasticKeyLen(cfg.mesh.primary_key,
                                                sizeof(cfg.mesh.primary_key),
                                                cfg.mesh.primary_key_len);
        if (key_len > 0)
        {
            uint8_t short_psk_index = 0;
            if (shortPskIndexForExpandedKey(cfg.mesh.primary_key, key_len, &short_psk_index))
            {
                channel.settings.psk.size = 1;
                channel.settings.psk.bytes[0] = short_psk_index;
            }
            else
            {
                channel.settings.psk.size = key_len;
                std::memcpy(channel.settings.psk.bytes,
                            cfg.mesh.primary_key,
                            key_len);
            }
        }
        else
        {
            channel.settings.psk.size = 1;
            channel.settings.psk.bytes[0] = 1;
        }
    }
    else
    {
        copyBounded(channel.settings.name,
                    sizeof(channel.settings.name),
                    chat::meshtastic::secondaryChannelName(cfg.mesh));
        channel.settings.id = cfg.mesh.secondary_channel_id;
        const size_t key_len = meshtasticKeyLen(cfg.mesh.secondary_key,
                                                sizeof(cfg.mesh.secondary_key),
                                                cfg.mesh.secondary_key_len);
        if (key_len > 0)
        {
            uint8_t short_psk_index = 0;
            if (shortPskIndexForExpandedKey(cfg.mesh.secondary_key, key_len, &short_psk_index))
            {
                channel.settings.psk.size = 1;
                channel.settings.psk.bytes[0] = short_psk_index;
            }
            else
            {
                channel.settings.psk.size = key_len;
                std::memcpy(channel.settings.psk.bytes,
                            cfg.mesh.secondary_key,
                            key_len);
            }
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
    const auto cfg = app_.getMeshtasticPhoneConfig();
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
    case meshtastic_AdminMessage_ConfigType_POWER_CONFIG:
        cfg_out.which_payload_variant = meshtastic_Config_power_tag;
        {
            meshtastic_Config_PowerConfig power = meshtastic_Config_PowerConfig_init_zero;
            cfg_out.payload_variant.power = power;
        }
        cfg_out.payload_variant.power.wait_bluetooth_secs = 0;
        cfg_out.payload_variant.power.ls_secs = 0;
        cfg_out.payload_variant.power.min_wake_secs = 0;
        break;
    case meshtastic_AdminMessage_ConfigType_NETWORK_CONFIG:
        cfg_out.which_payload_variant = meshtastic_Config_network_tag;
        {
            meshtastic_Config_NetworkConfig network = meshtastic_Config_NetworkConfig_init_zero;
            cfg_out.payload_variant.network = network;
        }
        cfg_out.payload_variant.network.wifi_enabled = false;
        cfg_out.payload_variant.network.eth_enabled = false;
        cfg_out.payload_variant.network.address_mode = meshtastic_Config_NetworkConfig_AddressMode_DHCP;
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
        if (!deferred_bluetooth_config_apply_pending_ && !admin_edit_transaction_bluetooth_dirty_)
        {
            cfg_out.payload_variant.bluetooth.enabled = app_.isBleEnabled();
        }
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
    case meshtastic_AdminMessage_ConfigType_SESSIONKEY_CONFIG:
        cfg_out.which_payload_variant = meshtastic_Config_sessionkey_tag;
        {
            meshtastic_Config_SessionkeyConfig sessionkey = meshtastic_Config_SessionkeyConfig_init_zero;
            cfg_out.payload_variant.sessionkey = sessionkey;
        }
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

void MeshtasticPhoneCore::fillPacketFromText(const chat::MeshIncomingText& msg, meshtastic_MeshPacket* out) const
{
    if (!out)
    {
        return;
    }
    meshtastic_MeshPacket& packet = *out;
    std::memset(&packet, 0, sizeof(packet));
    packet.from = msg.from;
    packet.to = msg.to;
    packet.channel = channelIndexFromId(msg.channel);
    packet.id = (msg.msg_id == 0) ? sys::millis_now() : msg.msg_id;
    packet.rx_time = (msg.rx_meta.rx_timestamp_s != 0) ? msg.rx_meta.rx_timestamp_s : msg.timestamp;
    packet.rx_snr = msg.rx_meta.snr_db_x10 / 10.0f;
    packet.rx_rssi = msg.rx_meta.rssi_dbm_x10 / 10;
    packet.hop_limit = msg.hop_limit;
    packet.via_mqtt = msg.rx_meta.from_is;
    packet.relay_node = msg.rx_meta.relay_node;
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    packet.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    packet.decoded.want_response = false;
    packet.decoded.payload.size = static_cast<pb_size_t>(
        std::min(msg.text.size(), sizeof(packet.decoded.payload.bytes)));
    if (packet.decoded.payload.size > 0)
    {
        std::memcpy(packet.decoded.payload.bytes, msg.text.data(), packet.decoded.payload.size);
    }
}

void MeshtasticPhoneCore::fillPacketFromData(const chat::MeshIncomingData& msg, meshtastic_MeshPacket* out) const
{
    if (!out)
    {
        return;
    }
    meshtastic_MeshPacket& packet = *out;
    std::memset(&packet, 0, sizeof(packet));
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
    packet.via_mqtt = msg.rx_meta.from_is;
    packet.relay_node = msg.rx_meta.relay_node;
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
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
}

} // namespace phone::meshtastic

#include "chat/infra/meshtastic/mt_node_payload.h"

#include "chat/infra/meshtastic/mt_protocol_helpers.h"
#include "meshtastic/config.pb.h"
#include "pb_decode.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace chat
{
namespace meshtastic
{
namespace
{

constexpr std::uint8_t kUnknownRole = 0xFF;

bool positionFromProto(const meshtastic_Position& pos,
                       std::uint32_t fallback_timestamp,
                       contacts::NodePosition* out);

std::string boundedString(const char* text, std::size_t max_len)
{
    if (text == nullptr || max_len == 0)
    {
        return {};
    }

    std::size_t len = 0;
    while (len < max_len && text[len] != '\0')
    {
        ++len;
    }
    return std::string(text, len);
}

bool hasAnyByte(const std::uint8_t* data, std::size_t len)
{
    if (data == nullptr)
    {
        return false;
    }
    for (std::size_t index = 0; index < len; ++index)
    {
        if (data[index] != 0)
        {
            return true;
        }
    }
    return false;
}

bool hasBoundedText(const char* text, std::size_t max_len)
{
    if (text == nullptr)
    {
        return false;
    }
    for (std::size_t index = 0; index < max_len; ++index)
    {
        if (text[index] == '\0')
        {
            return false;
        }
        if (text[index] != '\0')
        {
            return true;
        }
    }
    return false;
}

bool hasMeaningfulUserFacts(const meshtastic_User& user)
{
    return hasBoundedText(user.id, sizeof(user.id)) ||
           hasBoundedText(user.short_name, sizeof(user.short_name)) ||
           hasBoundedText(user.long_name, sizeof(user.long_name)) ||
           hasAnyByte(user.macaddr, sizeof(user.macaddr)) ||
           user.public_key.size > 0 ||
           user.hw_model != meshtastic_HardwareModel_UNSET ||
           user.is_licensed ||
           user.has_is_unmessagable;
}

bool hasMeaningfulNodeInfoFacts(const meshtastic_NodeInfo& node,
                                std::uint32_t fallback_timestamp)
{
    contacts::NodePosition ignored_position{};
    return node.num != 0 || (node.has_user && hasMeaningfulUserFacts(node.user)) ||
           (node.has_position &&
            positionFromProto(node.position,
                              fallback_timestamp,
                              &ignored_position)) ||
           node.snr != 0.0F || node.last_heard != 0 ||
           node.has_device_metrics || node.channel != 0 || node.via_mqtt ||
           node.has_hops_away || node.is_favorite || node.is_ignored ||
           node.is_key_manually_verified;
}

std::uint8_t sanitizeRole(meshtastic_Config_DeviceConfig_Role role)
{
    if (role <= meshtastic_Config_DeviceConfig_Role_CLIENT_BASE)
    {
        return static_cast<std::uint8_t>(role);
    }
    return kUnknownRole;
}

contacts::NodeDeviceMetrics deviceMetricsFromProto(
    const meshtastic_DeviceMetrics& metrics)
{
    contacts::NodeDeviceMetrics out{};
    out.has_battery_level = metrics.has_battery_level;
    out.battery_level = metrics.battery_level;
    out.has_voltage = metrics.has_voltage;
    out.voltage = metrics.voltage;
    out.has_channel_utilization = metrics.has_channel_utilization;
    out.channel_utilization = metrics.channel_utilization;
    out.has_air_util_tx = metrics.has_air_util_tx;
    out.air_util_tx = metrics.air_util_tx;
    out.has_uptime_seconds = metrics.has_uptime_seconds;
    out.uptime_seconds = metrics.uptime_seconds;
    return out;
}

bool positionFromProto(const meshtastic_Position& pos,
                       std::uint32_t fallback_timestamp,
                       contacts::NodePosition* out)
{
    if (out == nullptr || !hasValidPosition(pos))
    {
        return false;
    }

    contacts::NodePosition value{};
    value.valid = true;
    value.latitude_i = pos.latitude_i;
    value.longitude_i = pos.longitude_i;
    value.has_altitude = pos.has_altitude || pos.has_altitude_hae;
    value.altitude = pos.has_altitude ? pos.altitude : pos.altitude_hae;
    value.timestamp = pos.timestamp != 0
                          ? pos.timestamp
                          : (pos.time != 0 ? pos.time : fallback_timestamp);
    value.precision_bits = pos.precision_bits;
    value.pdop = pos.PDOP;
    value.hdop = pos.HDOP;
    value.vdop = pos.VDOP;
    value.gps_accuracy_mm = pos.gps_accuracy;
    *out = value;
    return true;
}

void applyUser(const meshtastic_User& user, DecodedNodePayload& out)
{
    out.has_user = true;
    out.short_name = boundedString(user.short_name, sizeof(user.short_name));
    out.long_name = boundedString(user.long_name, sizeof(user.long_name));
    out.role = sanitizeRole(user.role);
    out.hw_model = static_cast<std::uint8_t>(user.hw_model);

    out.has_macaddr = hasAnyByte(user.macaddr, sizeof(user.macaddr));
    if (out.has_macaddr)
    {
        std::copy(user.macaddr,
                  user.macaddr + sizeof(user.macaddr),
                  out.macaddr.begin());
    }

    out.has_public_key = user.public_key.size == out.public_key.size();
    if (out.has_public_key)
    {
        std::copy(user.public_key.bytes,
                  user.public_key.bytes + out.public_key.size(),
                  out.public_key.begin());
    }
}

DecodedNodePayload makeBasePayload(const NodePayloadDecodeContext& context)
{
    DecodedNodePayload out{};
    out.node_id = context.fallback_node_id;
    out.snr = context.snr;
    out.rssi = context.rssi;
    out.timestamp = context.timestamp;
    out.hops_away = context.hops_away;
    out.channel = context.channel;
    out.via_mqtt = context.via_mqtt;
    return out;
}

} // namespace

contacts::NodeUpdate DecodedNodePayload::toNodeUpdate() const
{
    contacts::NodeUpdate update{};
    update.short_name = short_name.c_str();
    update.long_name = long_name.c_str();
    update.has_last_seen = timestamp != 0;
    update.last_seen = timestamp;
    update.has_snr = !std::isnan(snr);
    update.snr = snr;
    update.has_rssi = !std::isnan(rssi);
    update.rssi = rssi;
    update.has_hops_away = hops_away != 0xFF;
    update.hops_away = hops_away;
    update.has_channel = channel != 0xFF;
    update.channel = channel;
    update.has_protocol = protocol != 0;
    update.protocol = protocol;
    update.has_role = role != kUnknownRole;
    update.role = role;
    update.has_hw_model = hw_model != 0;
    update.hw_model = hw_model;
    update.has_macaddr = has_macaddr;
    if (has_macaddr)
    {
        std::copy(macaddr.begin(), macaddr.end(), update.macaddr);
    }
    update.has_via_mqtt = true;
    update.via_mqtt = via_mqtt;
    update.has_is_ignored = true;
    update.is_ignored = is_ignored;
    update.has_public_key = has_user;
    update.public_key_present = has_public_key;
    // Trust is a local decision. Do not overwrite local trust state with
    // remote NODEINFO flags.
    update.has_key_manually_verified = false;
    update.has_device_metrics = has_device_metrics;
    if (has_device_metrics)
    {
        update.device_metrics = device_metrics;
    }
    update.has_position = has_position;
    if (has_position)
    {
        update.position = position;
    }
    return update;
}

bool decodeNodeInfoPayload(const meshtastic_Data& data,
                           const NodePayloadDecodeContext& context,
                           DecodedNodePayload* out)
{
    if (out == nullptr || data.portnum != meshtastic_PortNum_NODEINFO_APP ||
        data.payload.size == 0 ||
        data.payload.size > sizeof(data.payload.bytes))
    {
        return false;
    }

    meshtastic_NodeInfo node = meshtastic_NodeInfo_init_default;
    pb_istream_t node_stream =
        pb_istream_from_buffer(data.payload.bytes, data.payload.size);
    if (pb_decode(&node_stream, meshtastic_NodeInfo_fields, &node))
    {
        if (!hasMeaningfulNodeInfoFacts(node, context.timestamp))
        {
            return false;
        }
        DecodedNodePayload value = makeBasePayload(context);
        value.node_id = node.num != 0 ? node.num : context.fallback_node_id;
        if (std::isnan(value.snr) && node.snr != 0.0F)
        {
            value.snr = node.snr;
        }
        if (value.timestamp == 0 && node.last_heard != 0)
        {
            value.timestamp = node.last_heard;
        }
        value.via_mqtt = context.via_mqtt || node.via_mqtt;
        value.is_ignored = node.is_ignored;
        value.key_manually_verified = node.is_key_manually_verified;
        if (node.has_hops_away)
        {
            value.hops_away = static_cast<std::uint8_t>(node.hops_away);
        }
        if (node.has_user)
        {
            applyUser(node.user, value);
        }
        if (node.has_device_metrics)
        {
            value.has_device_metrics = true;
            value.device_metrics = deviceMetricsFromProto(node.device_metrics);
        }
        if (node.has_position)
        {
            value.has_position =
                positionFromProto(node.position, context.timestamp, &value.position);
        }
        *out = value;
        return true;
    }

    meshtastic_User user = meshtastic_User_init_default;
    pb_istream_t user_stream =
        pb_istream_from_buffer(data.payload.bytes, data.payload.size);
    if (!pb_decode(&user_stream, meshtastic_User_fields, &user))
    {
        return false;
    }
    if (!hasMeaningfulUserFacts(user))
    {
        return false;
    }

    DecodedNodePayload value = makeBasePayload(context);
    applyUser(user, value);
    *out = value;
    return true;
}

bool decodePositionPayload(const meshtastic_Data& data,
                           NodeId node_id,
                           std::uint32_t fallback_timestamp,
                           DecodedPositionPayload* out)
{
    if (out == nullptr || data.portnum != meshtastic_PortNum_POSITION_APP ||
        data.payload.size == 0 ||
        data.payload.size > sizeof(data.payload.bytes))
    {
        return false;
    }

    meshtastic_Position pos = meshtastic_Position_init_zero;
    pb_istream_t stream =
        pb_istream_from_buffer(data.payload.bytes, data.payload.size);
    contacts::NodePosition position{};
    if (!pb_decode(&stream, meshtastic_Position_fields, &pos) ||
        !positionFromProto(pos, fallback_timestamp, &position))
    {
        return false;
    }

    out->node_id = node_id;
    out->position = position;
    return true;
}

} // namespace meshtastic
} // namespace chat

#include "chat/infra/meshtastic/mt_node_payload.h"
#include "meshtastic/config.pb.h"
#include "pb_encode.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

namespace
{

int expect(bool condition, const char* message)
{
    if (condition)
    {
        return 0;
    }
    std::cerr << message << '\n';
    return 1;
}

bool encodeNodeInfoPayload(const meshtastic_NodeInfo& node,
                           meshtastic_Data* out)
{
    if (out == nullptr)
    {
        return false;
    }

    *out = meshtastic_Data_init_default;
    out->portnum = meshtastic_PortNum_NODEINFO_APP;

    pb_ostream_t stream =
        pb_ostream_from_buffer(out->payload.bytes, sizeof(out->payload.bytes));
    if (!pb_encode(&stream, meshtastic_NodeInfo_fields, &node))
    {
        return false;
    }
    out->payload.size = stream.bytes_written;
    return true;
}

bool encodeUserPayload(const meshtastic_User& user, meshtastic_Data* out)
{
    if (out == nullptr)
    {
        return false;
    }

    *out = meshtastic_Data_init_default;
    out->portnum = meshtastic_PortNum_NODEINFO_APP;

    pb_ostream_t stream =
        pb_ostream_from_buffer(out->payload.bytes, sizeof(out->payload.bytes));
    if (!pb_encode(&stream, meshtastic_User_fields, &user))
    {
        return false;
    }
    out->payload.size = stream.bytes_written;
    return true;
}

bool encodePositionPayload(const meshtastic_Position& position,
                           meshtastic_Data* out)
{
    if (out == nullptr)
    {
        return false;
    }

    *out = meshtastic_Data_init_default;
    out->portnum = meshtastic_PortNum_POSITION_APP;

    pb_ostream_t stream =
        pb_ostream_from_buffer(out->payload.bytes, sizeof(out->payload.bytes));
    if (!pb_encode(&stream, meshtastic_Position_fields, &position))
    {
        return false;
    }
    out->payload.size = stream.bytes_written;
    return true;
}

} // namespace

int main()
{
    ::chat::meshtastic::NodePayloadDecodeContext context{};
    context.fallback_node_id = 0xAABBCCDDU;
    context.snr = 7.5F;
    context.rssi = -81.25F;
    context.timestamp = 123456U;
    context.hops_away = 2;
    context.channel = 1;
    context.via_mqtt = false;

    meshtastic_NodeInfo node = meshtastic_NodeInfo_init_default;
    node.num = 0x01020304U;
    node.has_user = true;
    std::strncpy(node.user.short_name,
                 "ABCD",
                 sizeof(node.user.short_name) - 1);
    std::strncpy(node.user.long_name,
                 "Alpha Bravo",
                 sizeof(node.user.long_name) - 1);
    node.user.role = meshtastic_Config_DeviceConfig_Role_ROUTER;
    node.user.hw_model = meshtastic_HardwareModel_TBEAM;
    const std::array<std::uint8_t, 6> mac{
        {0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE}};
    std::memcpy(node.user.macaddr, mac.data(), mac.size());
    node.user.public_key.size = 32;
    for (std::size_t index = 0; index < node.user.public_key.size; ++index)
    {
        node.user.public_key.bytes[index] =
            static_cast<pb_byte_t>(index + 1U);
    }
    node.has_position = true;
    node.position.has_latitude_i = true;
    node.position.latitude_i = 312304000;
    node.position.has_longitude_i = true;
    node.position.longitude_i = 1214737000;
    node.position.has_altitude = true;
    node.position.altitude = 33;
    node.position.timestamp = 123400U;
    node.position.precision_bits = 24;
    node.position.HDOP = 88;
    node.has_device_metrics = true;
    node.device_metrics.has_battery_level = true;
    node.device_metrics.battery_level = 86;
    node.device_metrics.has_voltage = true;
    node.device_metrics.voltage = 4.12F;
    node.channel = 1;
    node.via_mqtt = true;
    node.has_hops_away = true;
    node.hops_away = 3;
    node.is_ignored = true;
    node.is_key_manually_verified = true;

    meshtastic_Data node_data = meshtastic_Data_init_default;
    if (int rc = expect(encodeNodeInfoPayload(node, &node_data),
                        "failed to encode NodeInfo test payload"))
    {
        return rc;
    }

    ::chat::meshtastic::DecodedNodePayload decoded_node{};
    if (int rc = expect(::chat::meshtastic::decodeNodeInfoPayload(
                            node_data, context, &decoded_node),
                        "shared decoder rejected full NodeInfo payload"))
    {
        return rc;
    }
    if (int rc = expect(decoded_node.node_id == node.num,
                        "NodeInfo num was not preferred over header sender"))
    {
        return rc;
    }
    if (int rc = expect(decoded_node.has_user && decoded_node.short_name == "ABCD",
                        "NodeInfo short name was not decoded"))
    {
        return rc;
    }
    if (int rc = expect(decoded_node.long_name == "Alpha Bravo",
                        "NodeInfo long name was not decoded"))
    {
        return rc;
    }
    if (int rc = expect(decoded_node.has_position &&
                            decoded_node.position.latitude_i ==
                                node.position.latitude_i &&
                            decoded_node.position.longitude_i ==
                                node.position.longitude_i,
                        "NodeInfo embedded position was not decoded"))
    {
        return rc;
    }
    if (int rc = expect(decoded_node.via_mqtt,
                        "NodeInfo via_mqtt flag was not decoded"))
    {
        return rc;
    }
    if (int rc = expect(decoded_node.has_device_metrics &&
                            decoded_node.device_metrics.battery_level == 86,
                        "NodeInfo device metrics were not decoded"))
    {
        return rc;
    }
    if (int rc = expect(decoded_node.has_public_key &&
                            decoded_node.public_key[0] == 1U &&
                            decoded_node.public_key[31] == 32U,
                        "NodeInfo public key was not decoded"))
    {
        return rc;
    }

    context.snr = std::numeric_limits<float>::quiet_NaN();
    context.timestamp = 0;
    node.snr = 6.25F;
    node.last_heard = 777U;
    if (int rc = expect(encodeNodeInfoPayload(node, &node_data),
                        "failed to encode NodeInfo fallback payload"))
    {
        return rc;
    }
    ::chat::meshtastic::DecodedNodePayload decoded_fallback{};
    if (int rc = expect(::chat::meshtastic::decodeNodeInfoPayload(
                            node_data, context, &decoded_fallback),
                        "shared decoder rejected NodeInfo fallback payload"))
    {
        return rc;
    }
    if (int rc = expect(std::fabs(decoded_fallback.snr - 6.25F) < 0.001F &&
                            decoded_fallback.timestamp == 777U,
                        "NodeInfo snr/last_heard fallback was not decoded"))
    {
        return rc;
    }
    context.snr = 7.5F;
    context.timestamp = 123456U;

    const auto update = decoded_node.toNodeUpdate();
    if (int rc = expect(update.has_position && update.position.valid,
                        "NodeUpdate projection lost position"))
    {
        return rc;
    }
    if (int rc = expect(update.has_via_mqtt && update.via_mqtt,
                        "NodeUpdate projection lost via_mqtt"))
    {
        return rc;
    }
    if (int rc = expect(update.has_snr && update.has_rssi &&
                            update.has_hops_away && update.has_channel &&
                            update.has_role && update.has_hw_model,
                        "NodeUpdate projection did not preserve known facts"))
    {
        return rc;
    }

    ::chat::meshtastic::DecodedNodePayload unknown_projection{};
    unknown_projection.snr = std::numeric_limits<float>::quiet_NaN();
    unknown_projection.rssi = std::numeric_limits<float>::quiet_NaN();
    unknown_projection.hops_away = 0xFF;
    unknown_projection.channel = 0xFF;
    unknown_projection.role = 0xFF;
    const auto unknown_update = unknown_projection.toNodeUpdate();
    if (int rc = expect(!unknown_update.has_last_seen &&
                            !unknown_update.has_snr &&
                            !unknown_update.has_rssi &&
                            !unknown_update.has_hops_away &&
                            !unknown_update.has_channel &&
                            !unknown_update.has_role &&
                            !unknown_update.has_hw_model &&
                            !unknown_update.has_public_key,
                        "NodeUpdate projection marked unknown facts as present"))
    {
        return rc;
    }

    meshtastic_User legacy_user = meshtastic_User_init_default;
    std::strncpy(legacy_user.short_name,
                 "LG",
                 sizeof(legacy_user.short_name) - 1);
    std::strncpy(legacy_user.long_name,
                 "Legacy User",
                 sizeof(legacy_user.long_name) - 1);
    legacy_user.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    legacy_user.hw_model = meshtastic_HardwareModel_T_ECHO;

    meshtastic_Data legacy_data = meshtastic_Data_init_default;
    if (int rc = expect(encodeUserPayload(legacy_user, &legacy_data),
                        "failed to encode legacy User test payload"))
    {
        return rc;
    }

    context.via_mqtt = true;
    ::chat::meshtastic::DecodedNodePayload decoded_legacy{};
    if (int rc = expect(::chat::meshtastic::decodeNodeInfoPayload(
                            legacy_data, context, &decoded_legacy),
                        "shared decoder rejected legacy User payload"))
    {
        return rc;
    }
    if (int rc = expect(decoded_legacy.node_id == context.fallback_node_id,
                        "legacy User did not keep header sender as node id"))
    {
        return rc;
    }
    if (int rc = expect(decoded_legacy.has_user &&
                            decoded_legacy.short_name == "LG" &&
                            decoded_legacy.long_name == "Legacy User",
                        "legacy User names were not decoded"))
    {
        return rc;
    }
    if (int rc = expect(decoded_legacy.via_mqtt,
                        "legacy User did not inherit header via_mqtt"))
    {
        return rc;
    }

    meshtastic_Data invalid_user_data = meshtastic_Data_init_default;
    invalid_user_data.portnum = meshtastic_PortNum_NODEINFO_APP;
    invalid_user_data.payload.size = 1;
    invalid_user_data.payload.bytes[0] = 0;
    ::chat::meshtastic::DecodedNodePayload invalid_node{};
    if (int rc = expect(!::chat::meshtastic::decodeNodeInfoPayload(
                            invalid_user_data, context, &invalid_node),
                        "shared decoder accepted an empty NodeInfo/User payload"))
    {
        return rc;
    }

    meshtastic_Position position = meshtastic_Position_init_zero;
    position.has_latitude_i = true;
    position.latitude_i = 223344550;
    position.has_longitude_i = true;
    position.longitude_i = 1142233440;
    position.has_altitude_hae = true;
    position.altitude_hae = 99;

    meshtastic_Data position_data = meshtastic_Data_init_default;
    if (int rc = expect(encodePositionPayload(position, &position_data),
                        "failed to encode Position test payload"))
    {
        return rc;
    }

    ::chat::meshtastic::DecodedPositionPayload decoded_position{};
    if (int rc = expect(::chat::meshtastic::decodePositionPayload(
                            position_data,
                            0x0BADCAFEU,
                            999U,
                            &decoded_position),
                        "shared decoder rejected Position payload"))
    {
        return rc;
    }
    if (int rc = expect(decoded_position.node_id == 0x0BADCAFEU &&
                            decoded_position.position.valid &&
                            decoded_position.position.has_altitude &&
                            decoded_position.position.altitude == 99 &&
                            decoded_position.position.timestamp == 999U,
                        "Position payload projection was wrong"))
    {
        return rc;
    }

    return 0;
}

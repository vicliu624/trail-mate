/**
 * @file mt_protocol_helpers.cpp
 * @brief Shared Meshtastic protocol helper utilities
 */

#include "chat/infra/meshtastic/mt_protocol_helpers.h"

#include "pb_decode.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace chat
{
namespace meshtastic
{
namespace
{
constexpr uint32_t kBroadcastNodeId = 0xFFFFFFFFu;
constexpr size_t kTraceRouteSlots = 8;
constexpr uint8_t kDefaultPsk[16] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                     0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};

void selectTraceRouteArrays(meshtastic_RouteDiscovery* route,
                            bool towards_destination,
                            pb_size_t** route_count,
                            uint32_t** route_nodes,
                            pb_size_t** snr_count,
                            int8_t** snr_values)
{
    if (towards_destination)
    {
        *route_count = &route->route_count;
        *route_nodes = route->route;
        *snr_count = &route->snr_towards_count;
        *snr_values = route->snr_towards;
    }
    else
    {
        *route_count = &route->route_back_count;
        *route_nodes = route->route_back;
        *snr_count = &route->snr_back_count;
        *snr_values = route->snr_back;
    }
}

int8_t encodeTraceRouteSnr(const chat::RxMeta* rx_meta)
{
    if (!rx_meta)
    {
        return 0;
    }

    const long scaled = std::lround((static_cast<float>(rx_meta->snr_db_x10) / 10.0f) * 4.0f);
    const long min_v = static_cast<long>(std::numeric_limits<int8_t>::min());
    const long max_v = static_cast<long>(std::numeric_limits<int8_t>::max());
    const long clamped = (scaled < min_v) ? min_v : ((scaled > max_v) ? max_v : scaled);
    return static_cast<int8_t>(clamped);
}

uint8_t xorHash(const uint8_t* data, size_t len)
{
    uint8_t out = 0;
    for (size_t i = 0; i < len; ++i)
    {
        out ^= data[i];
    }
    return out;
}

} // namespace

bool shouldSetAirWantAck(uint32_t dest, bool want_ack)
{
    return want_ack && dest != kBroadcastNodeId;
}

const char* keyVerificationStage(const meshtastic_KeyVerification& kv)
{
    const size_t hash1_len = kv.hash1.size;
    const size_t hash2_len = kv.hash2.size;
    if (hash1_len == 0 && hash2_len == 0)
    {
        return "INIT";
    }
    if (hash1_len == 0 && hash2_len == 32)
    {
        return "REPLY";
    }
    if (hash1_len == 32)
    {
        return "FINAL";
    }
    return "UNKNOWN";
}

const char* routingErrorName(meshtastic_Routing_Error err)
{
    switch (err)
    {
    case meshtastic_Routing_Error_NONE:
        return "NONE";
    case meshtastic_Routing_Error_NO_ROUTE:
        return "NO_ROUTE";
    case meshtastic_Routing_Error_GOT_NAK:
        return "GOT_NAK";
    case meshtastic_Routing_Error_TIMEOUT:
        return "TIMEOUT";
    case meshtastic_Routing_Error_NO_INTERFACE:
        return "NO_INTERFACE";
    case meshtastic_Routing_Error_MAX_RETRANSMIT:
        return "MAX_RETRANSMIT";
    case meshtastic_Routing_Error_NO_CHANNEL:
        return "NO_CHANNEL";
    case meshtastic_Routing_Error_TOO_LARGE:
        return "TOO_LARGE";
    case meshtastic_Routing_Error_NO_RESPONSE:
        return "NO_RESPONSE";
    case meshtastic_Routing_Error_DUTY_CYCLE_LIMIT:
        return "DUTY_CYCLE_LIMIT";
    case meshtastic_Routing_Error_BAD_REQUEST:
        return "BAD_REQUEST";
    case meshtastic_Routing_Error_NOT_AUTHORIZED:
        return "NOT_AUTHORIZED";
    case meshtastic_Routing_Error_PKI_FAILED:
        return "PKI_FAILED";
    case meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY:
        return "PKI_UNKNOWN_PUBKEY";
    case meshtastic_Routing_Error_ADMIN_BAD_SESSION_KEY:
        return "ADMIN_BAD_SESSION_KEY";
    case meshtastic_Routing_Error_ADMIN_PUBLIC_KEY_UNAUTHORIZED:
        return "ADMIN_PUBLIC_KEY_UNAUTHORIZED";
    case meshtastic_Routing_Error_RATE_LIMIT_EXCEEDED:
        return "RATE_LIMIT_EXCEEDED";
    default:
        return "UNKNOWN";
    }
}

bool hasValidPosition(const meshtastic_Position& pos)
{
    return pos.has_latitude_i && pos.has_longitude_i;
}

void expandShortPsk(uint8_t index, uint8_t* out, size_t* out_len)
{
    if (!out || !out_len || index == 0)
    {
        if (out_len)
        {
            *out_len = 0;
        }
        return;
    }
    memcpy(out, kDefaultPsk, sizeof(kDefaultPsk));
    out[sizeof(kDefaultPsk) - 1] = static_cast<uint8_t>(out[sizeof(kDefaultPsk) - 1] + index - 1);
    *out_len = sizeof(kDefaultPsk);
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

uint8_t computeChannelHash(const char* name, const uint8_t* key, size_t key_len)
{
    uint8_t h = xorHash(reinterpret_cast<const uint8_t*>(name), strlen(name));
    if (key && key_len > 0)
    {
        h ^= xorHash(key, key_len);
    }
    return h;
}

std::string toHex(const uint8_t* data, size_t len, size_t max_len)
{
    if (!data || len == 0)
    {
        return {};
    }
    size_t capped = (len > max_len) ? max_len : len;
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(capped * 2);
    for (size_t i = 0; i < capped; ++i)
    {
        uint8_t b = data[i];
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0F]);
    }
    if (capped < len)
    {
        out.append("..");
    }
    return out;
}

uint8_t computeHopsAway(uint8_t flags)
{
    uint8_t hop_limit = flags & PACKET_FLAGS_HOP_LIMIT_MASK;
    uint8_t hop_start = (flags & PACKET_FLAGS_HOP_START_MASK) >> PACKET_FLAGS_HOP_START_SHIFT;
    if (hop_start == 0 && hop_limit == 0)
    {
        return 0xFF;
    }
    return (hop_start >= hop_limit) ? static_cast<uint8_t>(hop_start - hop_limit) : 0;
}

void insertTraceRouteUnknownHops(uint8_t flags,
                                 meshtastic_RouteDiscovery* route,
                                 bool towards_destination)
{
    if (!route)
    {
        return;
    }

    pb_size_t* route_count = nullptr;
    uint32_t* route_nodes = nullptr;
    pb_size_t* snr_count = nullptr;
    int8_t* snr_values = nullptr;
    selectTraceRouteArrays(route, towards_destination, &route_count, &route_nodes, &snr_count, &snr_values);

    const uint8_t hops_taken = computeHopsAway(flags);
    if (hops_taken == 0xFF)
    {
        return;
    }

    int route_missing = static_cast<int>(hops_taken) - static_cast<int>(*route_count);
    while (route_missing-- > 0 && *route_count < kTraceRouteSlots)
    {
        route_nodes[*route_count] = kBroadcastNodeId;
        *route_count += 1;
    }

    int snr_missing = static_cast<int>(*route_count) - static_cast<int>(*snr_count);
    while (snr_missing-- > 0 && *snr_count < kTraceRouteSlots)
    {
        snr_values[*snr_count] = std::numeric_limits<int8_t>::min();
        *snr_count += 1;
    }
}

void appendTraceRouteNodeAndSnr(meshtastic_RouteDiscovery* route,
                                uint32_t node_id,
                                const chat::RxMeta* rx_meta,
                                bool towards_destination,
                                bool snr_only)
{
    if (!route)
    {
        return;
    }

    pb_size_t* route_count = nullptr;
    uint32_t* route_nodes = nullptr;
    pb_size_t* snr_count = nullptr;
    int8_t* snr_values = nullptr;
    selectTraceRouteArrays(route, towards_destination, &route_count, &route_nodes, &snr_count, &snr_values);

    if (*snr_count < kTraceRouteSlots)
    {
        snr_values[*snr_count] = encodeTraceRouteSnr(rx_meta);
        *snr_count += 1;
    }

    if (snr_only)
    {
        return;
    }

    if (*route_count < kTraceRouteSlots)
    {
        route_nodes[*route_count] = node_id;
        *route_count += 1;
    }
}

bool readPbString(pb_istream_t* stream, char* out, size_t out_len)
{
    if (!stream || !out || out_len == 0)
    {
        return false;
    }
    size_t to_read = stream->bytes_left;
    if (to_read >= out_len)
    {
        to_read = out_len - 1;
    }
    if (to_read > 0 && !pb_read(stream, reinterpret_cast<pb_byte_t*>(out), to_read))
    {
        return false;
    }
    out[to_read] = '\0';
    size_t remaining = stream->bytes_left;
    if (remaining > 0)
    {
        uint8_t discard[32];
        while (remaining > 0)
        {
            size_t chunk = std::min(remaining, sizeof(discard));
            if (!pb_read(stream, discard, chunk))
            {
                return false;
            }
            remaining -= chunk;
        }
    }
    return true;
}

bool makeEncryptedPacketFromWire(const uint8_t* wire_data, size_t wire_size,
                                 meshtastic_MeshPacket* out_packet)
{
    if (!wire_data || !out_packet || wire_size < sizeof(PacketHeaderWire))
    {
        return false;
    }

    PacketHeaderWire header{};
    uint8_t payload[256];
    size_t payload_size = sizeof(payload);
    if (!parseWirePacket(wire_data, wire_size, &header, payload, &payload_size))
    {
        return false;
    }

    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    packet.from = header.from;
    packet.to = header.to;
    packet.channel = header.channel;
    packet.id = header.id;
    packet.hop_limit = header.flags & PACKET_FLAGS_HOP_LIMIT_MASK;
    packet.want_ack = (header.flags & PACKET_FLAGS_WANT_ACK_MASK) != 0;
    packet.via_mqtt = (header.flags & PACKET_FLAGS_VIA_MQTT_MASK) != 0;
    packet.hop_start = (header.flags & PACKET_FLAGS_HOP_START_MASK) >> PACKET_FLAGS_HOP_START_SHIFT;
    packet.next_hop = header.next_hop;
    packet.relay_node = header.relay_node;
    packet.pki_encrypted = (header.channel == 0);
    packet.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    packet.encrypted.size = static_cast<pb_size_t>(std::min(payload_size, sizeof(packet.encrypted.bytes)));
    memcpy(packet.encrypted.bytes, payload, packet.encrypted.size);
    *out_packet = packet;
    return true;
}

void fillDecodedPacketCommon(meshtastic_MeshPacket* packet,
                             const meshtastic_Data& decoded,
                             const PacketHeaderWire& header,
                             chat::ChannelId channel_index)
{
    if (!packet)
    {
        return;
    }
    packet->from = header.from;
    packet->to = header.to;
    packet->channel = (channel_index == chat::ChannelId::SECONDARY) ? 1 : 0;
    packet->id = header.id;
    packet->hop_limit = header.flags & PACKET_FLAGS_HOP_LIMIT_MASK;
    packet->want_ack = (header.flags & PACKET_FLAGS_WANT_ACK_MASK) != 0;
    packet->via_mqtt = (header.flags & PACKET_FLAGS_VIA_MQTT_MASK) != 0;
    packet->hop_start = (header.flags & PACKET_FLAGS_HOP_START_MASK) >> PACKET_FLAGS_HOP_START_SHIFT;
    packet->next_hop = header.next_hop;
    packet->relay_node = header.relay_node;
    packet->which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    packet->decoded = decoded;
}

bool allowPkiForPortnum(uint32_t portnum)
{
    return portnum != meshtastic_PortNum_NODEINFO_APP &&
           portnum != meshtastic_PortNum_ROUTING_APP &&
           portnum != meshtastic_PortNum_POSITION_APP &&
           portnum != meshtastic_PortNum_TRACEROUTE_APP;
}

uint32_t djb2HashText(const char* text)
{
    if (!text)
    {
        return 0;
    }
    uint32_t hash = 5381;
    int c = 0;
    while ((c = *text++) != 0)
    {
        hash = ((hash << 5) + hash) + static_cast<uint8_t>(c);
    }
    return hash;
}

void modemPresetToParams(meshtastic_Config_LoRaConfig_ModemPreset preset, bool wide_lora,
                         float& bw_khz, uint8_t& sf, uint8_t& cr_denom)
{
    switch (preset)
    {
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO:
        bw_khz = wide_lora ? 1625.0f : 500.0f;
        cr_denom = 5;
        sf = 7;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:
        bw_khz = wide_lora ? 812.5f : 250.0f;
        cr_denom = 5;
        sf = 7;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:
        bw_khz = wide_lora ? 812.5f : 250.0f;
        cr_denom = 5;
        sf = 8;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:
        bw_khz = wide_lora ? 812.5f : 250.0f;
        cr_denom = 5;
        sf = 9;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:
        bw_khz = wide_lora ? 812.5f : 250.0f;
        cr_denom = 5;
        sf = 10;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO:
        bw_khz = wide_lora ? 1625.0f : 500.0f;
        cr_denom = 8;
        sf = 11;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:
        bw_khz = wide_lora ? 406.25f : 125.0f;
        cr_denom = 8;
        sf = 11;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:
        bw_khz = wide_lora ? 406.25f : 125.0f;
        cr_denom = 8;
        sf = 12;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST:
    default:
        bw_khz = wide_lora ? 812.5f : 250.0f;
        cr_denom = 5;
        sf = 11;
        break;
    }
}

} // namespace meshtastic
} // namespace chat
/**
 * @file chat_types.h
 * @brief Core type definitions for chat functionality
 */

#pragma once

#include <Arduino.h>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace chat
{

/**
 * @brief Channel identifier
 * Primary channel (0) is the default public channel
 */
enum class ChannelId : uint8_t
{
    PRIMARY = 0,   // Public channel (default broadcast)
    SECONDARY = 1, // Squad channel (encrypted)
    MAX_CHANNELS = 3
};

/**
 * @brief Node identifier (32-bit, compatible with Meshtastic)
 */
using NodeId = uint32_t;

/**
 * @brief Message identifier (32-bit)
 */
using MessageId = uint32_t;

/**
 * @brief Mesh protocol selection
 */
enum class MeshProtocol : uint8_t
{
    Meshtastic = 1,
    MeshCore = 2
};

/**
 * @brief Active discovery actions (protocol-specific)
 */
enum class MeshDiscoveryAction : uint8_t
{
    ScanLocal = 1,
    SendIdLocal = 2,
    SendIdBroadcast = 3
};

/**
 * @brief RX time source for received packets
 */
enum class RxTimeSource : uint8_t
{
    Unknown = 0,
    Uptime = 1,
    DeviceUtc = 2,
    GpsUtc = 3
};

/**
 * @brief RX origin classification
 */
enum class RxOrigin : uint8_t
{
    Unknown = 0,
    Mesh = 1,
    External = 2
};

/**
 * @brief RX metadata for mesh packets
 */
struct RxMeta
{
    uint32_t rx_timestamp_s;
    uint32_t rx_timestamp_ms;
    RxTimeSource time_source;
    RxOrigin origin;
    bool direct;
    bool from_is;
    uint8_t hop_count;
    uint8_t hop_limit;
    uint8_t channel_hash;
    uint8_t wire_flags;
    int16_t rssi_dbm_x10;
    int16_t snr_db_x10;
    uint32_t freq_hz;
    uint32_t bw_hz;
    uint8_t sf;
    uint8_t cr;
    uint32_t next_hop;
    uint32_t relay_node;

    RxMeta()
        : rx_timestamp_s(0),
          rx_timestamp_ms(0),
          time_source(RxTimeSource::Unknown),
          origin(RxOrigin::Unknown),
          direct(false),
          from_is(false),
          hop_count(0xFF),
          hop_limit(0xFF),
          channel_hash(0xFF),
          wire_flags(0xFF),
          rssi_dbm_x10(std::numeric_limits<int16_t>::min()),
          snr_db_x10(std::numeric_limits<int16_t>::min()),
          freq_hz(0),
          bw_hz(0),
          sf(0),
          cr(0),
          next_hop(0),
          relay_node(0)
    {
    }
};

/**
 * @brief Conversation identifier (channel + peer)
 * peer = 0 means channel-wide/broadcast
 */
struct ConversationId
{
    MeshProtocol protocol;
    ChannelId channel;
    NodeId peer; // 0 for broadcast/channel thread

    ConversationId(ChannelId ch = ChannelId::PRIMARY,
                   NodeId p = 0,
                   MeshProtocol proto = MeshProtocol::Meshtastic)
        : protocol(proto), channel(ch), peer(p) {}

    bool operator<(const ConversationId& other) const
    {
        if (protocol != other.protocol)
        {
            return static_cast<uint8_t>(protocol) < static_cast<uint8_t>(other.protocol);
        }
        if (channel != other.channel)
        {
            return static_cast<uint8_t>(channel) < static_cast<uint8_t>(other.channel);
        }
        return peer < other.peer;
    }
    bool operator==(const ConversationId& other) const
    {
        return protocol == other.protocol &&
               channel == other.channel &&
               peer == other.peer;
    }
};

/**
 * @brief Message status
 */
enum class MessageStatus
{
    Incoming, // Received message
    Queued,   // Queued for sending
    Sent,     // Successfully sent
    Failed    // Failed to send
};

/**
 * @brief Chat message structure
 */
struct ChatMessage
{
    MeshProtocol protocol;
    ChannelId channel;
    NodeId from; // 0 for local messages
    NodeId peer; // conversation peer (0 for broadcast)
    MessageId msg_id;
    uint32_t timestamp; // Unix timestamp (seconds)
    std::string text;
    uint8_t team_location_icon; // Team location semantic icon id (0 = none)
    bool has_geo;
    int32_t geo_lat_e7;
    int32_t geo_lon_e7;
    MessageStatus status;

    ChatMessage() : protocol(MeshProtocol::Meshtastic),
                    channel(ChannelId::PRIMARY), from(0), peer(0), msg_id(0),
                    timestamp(0),
                    team_location_icon(0),
                    has_geo(false),
                    geo_lat_e7(0),
                    geo_lon_e7(0),
                    status(MessageStatus::Incoming) {}
};

/**
 * @brief Conversation metadata for UI
 */
struct ConversationMeta
{
    ConversationId id;
    std::string name;
    std::string preview;
    uint32_t last_timestamp;
    int unread;

    ConversationMeta() : id(), last_timestamp(0), unread(0) {}
};

/**
 * @brief Incoming text message from mesh
 */
struct MeshIncomingText
{
    ChannelId channel;
    NodeId from;
    NodeId to;
    MessageId msg_id;
    uint32_t timestamp;
    std::string text;
    uint8_t hop_limit; // Remaining hops
    bool encrypted;    // Whether message was encrypted
    RxMeta rx_meta;
};

/**
 * @brief Incoming non-text mesh payload
 */
struct MeshIncomingData
{
    uint32_t portnum;
    NodeId from;
    NodeId to;
    MessageId packet_id;
    ChannelId channel;
    uint8_t channel_hash;
    bool want_response;
    std::vector<uint8_t> payload;
    RxMeta rx_meta;

    MeshIncomingData()
        : portnum(0), from(0), to(0), packet_id(0),
          channel(ChannelId::PRIMARY), channel_hash(0),
          want_response(false) {}
};

/**
 * @brief Mesh configuration
 */
struct MeshConfig
{
    // Meshtastic radio configuration
    uint8_t region;           // LoRa region (0=US, 1=EU, etc.)
    bool use_preset;          // true: use modem_preset, false: use manual params
    uint8_t modem_preset;     // Modem preset index
    float bandwidth_khz;      // Manual bandwidth when use_preset=false
    uint8_t spread_factor;    // Manual SF when use_preset=false
    uint8_t coding_rate;      // Manual CR denominator (5..8) when use_preset=false
    int8_t tx_power;          // TX power in dBm
    uint8_t hop_limit;        // Maximum hop limit (1-7)
    bool tx_enabled;          // Disable TX when false
    bool override_duty_cycle; // Ignore duty cycle based throttling when true
    uint16_t channel_num;     // 0 = auto hash, otherwise 1..N channel slot
    float frequency_offset_mhz;
    float override_frequency_mhz; // 0 = disabled
    bool enable_relay;            // Reserved relay switch (not currently routed)

    // Channel encryption keys (PSK for encrypted channels)
    uint8_t primary_key[16];   // Primary channel key (usually empty for public)
    uint8_t secondary_key[16]; // Secondary channel key (Squad PSK)

    // MeshCore radio/channel tuning
    uint8_t meshcore_region_preset; // 0=Custom, >0 preset id
    float meshcore_freq_mhz;
    float meshcore_bw_khz;
    uint8_t meshcore_sf;
    uint8_t meshcore_cr;
    bool meshcore_client_repeat;
    float meshcore_rx_delay_base;
    float meshcore_airtime_factor;
    uint8_t meshcore_flood_max;
    bool meshcore_multi_acks;
    uint8_t meshcore_channel_slot;
    char meshcore_channel_name[32];

    MeshConfig()
        : region(0),
          use_preset(true),
          modem_preset(0),
          bandwidth_khz(250.0f),
          spread_factor(11),
          coding_rate(5),
          tx_power(14),
          hop_limit(2),
          tx_enabled(true),
          override_duty_cycle(false),
          channel_num(0),
          frequency_offset_mhz(0.0f),
          override_frequency_mhz(0.0f),
          enable_relay(true),
          meshcore_region_preset(0),
          meshcore_freq_mhz(915.0f),
          meshcore_bw_khz(125.0f),
          meshcore_sf(9),
          meshcore_cr(5),
          meshcore_client_repeat(false),
          meshcore_rx_delay_base(0.0f),
          meshcore_airtime_factor(1.0f),
          meshcore_flood_max(16),
          meshcore_multi_acks(false),
          meshcore_channel_slot(0)
    {
        memset(primary_key, 0, 16);
        memset(secondary_key, 0, 16);
        meshcore_channel_name[0] = '\0';
    }
};

} // namespace chat

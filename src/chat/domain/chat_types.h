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
    ChannelId channel;
    NodeId peer; // 0 for broadcast/channel thread

    ConversationId(ChannelId ch = ChannelId::PRIMARY, NodeId p = 0)
        : channel(ch), peer(p) {}

    bool operator<(const ConversationId& other) const
    {
        if (channel != other.channel)
        {
            return static_cast<uint8_t>(channel) < static_cast<uint8_t>(other.channel);
        }
        return peer < other.peer;
    }
    bool operator==(const ConversationId& other) const
    {
        return channel == other.channel && peer == other.peer;
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
    ChannelId channel;
    NodeId from; // 0 for local messages
    NodeId peer; // conversation peer (0 for broadcast)
    MessageId msg_id;
    uint32_t timestamp; // Unix timestamp (seconds)
    std::string text;
    MessageStatus status;

    ChatMessage() : channel(ChannelId::PRIMARY), from(0), peer(0), msg_id(0),
                    timestamp(0), status(MessageStatus::Incoming) {}
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
    uint8_t region;       // LoRa region (0=US, 1=EU, etc.)
    uint8_t modem_preset; // Modem preset index
    int8_t tx_power;      // TX power in dBm
    uint8_t hop_limit;    // Maximum hop limit (1-3)
    bool enable_relay;    // Enable message relay/forwarding

    // Channel encryption keys (PSK for encrypted channels)
    uint8_t primary_key[16];   // Primary channel key (usually empty for public)
    uint8_t secondary_key[16]; // Secondary channel key (Squad PSK)

    MeshConfig() : region(0), modem_preset(0), tx_power(14),
                   hop_limit(2), enable_relay(true)
    {
        memset(primary_key, 0, 16);
        memset(secondary_key, 0, 16);
    }
};

/**
 * @brief Mesh protocol selection
 */
enum class MeshProtocol : uint8_t
{
    Meshtastic = 1,
    MeshCore = 2
};

} // namespace chat

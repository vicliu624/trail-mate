/**
 * @file chat_types.h
 * @brief Core type definitions for chat functionality
 */

#pragma once

#include <Arduino.h>
#include <cstring>
#include <string>

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
    MessageId msg_id;
    uint32_t timestamp;
    std::string text;
    uint8_t hop_limit; // Remaining hops
    bool encrypted;    // Whether message was encrypted
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

/**
 * @file mt_packet_wire.h
 * @brief Meshtastic wire packet format (like M5Tab5-GPS)
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace chat {
namespace meshtastic {

/**
 * @brief Packet header wire format (from M5Tab5-GPS)
 * This matches Meshtastic's on-air packet format
 */
struct PacketHeaderWire {
    uint32_t to;
    uint32_t from;
    uint32_t id;
    uint8_t flags;
    uint8_t channel;
    uint8_t next_hop;
    uint8_t relay_node;
} __attribute__((packed));

// Packet header flag masks (from M5Tab5-GPS)
constexpr uint8_t PACKET_FLAGS_HOP_LIMIT_MASK  = 0x07;
constexpr uint8_t PACKET_FLAGS_WANT_ACK_MASK   = 0x08;
constexpr uint8_t PACKET_FLAGS_VIA_MQTT_MASK   = 0x10;
constexpr uint8_t PACKET_FLAGS_HOP_START_MASK  = 0xE0;
constexpr uint8_t PACKET_FLAGS_HOP_START_SHIFT = 5;

/**
 * @brief Build full packet with wire header + encrypted payload
 * @param data_payload Encoded Data message payload
 * @param data_len Data payload length
 * @param from_node Source node ID
 * @param packet_id Packet ID
 * @param dest_node Destination node ID (0xFFFFFFFF = broadcast)
 * @param channel_hash Channel hash
 * @param hop_limit Hop limit
 * @param want_ack Want ACK flag
 * @param psk Encryption key (nullptr if no encryption)
 * @param psk_len Key length
 * @param out_buffer Output buffer
 * @param out_size Output buffer size (updated with actual size)
 * @return true if successful
 */
bool buildWirePacket(const uint8_t* data_payload, size_t data_len,
                    uint32_t from_node, uint32_t packet_id,
                    uint32_t dest_node, uint8_t channel_hash,
                    uint8_t hop_limit, bool want_ack,
                    const uint8_t* psk, size_t psk_len,
                    uint8_t* out_buffer, size_t* out_size);

/**
 * @brief Parse wire packet to extract header and payload
 * @param buffer Packet buffer
 * @param size Packet size
 * @param out_header Output header
 * @param out_payload Output payload buffer
 * @param out_payload_size Output payload size
 * @return true if successful
 */
bool parseWirePacket(const uint8_t* buffer, size_t size,
                    PacketHeaderWire* out_header,
                    uint8_t* out_payload, size_t* out_payload_size);

/**
 * @brief Decrypt payload using AES-CTR (like M5Tab5-GPS)
 * @param header Packet header
 * @param cipher Encrypted payload
 * @param cipher_len Cipher length
 * @param psk Encryption key
 * @param psk_len Key length
 * @param out_plaintext Output plaintext buffer
 * @param out_plain_len Output plaintext length
 * @return true if successful
 */
bool decryptPayload(const PacketHeaderWire& header,
                   const uint8_t* cipher, size_t cipher_len,
                   const uint8_t* psk, size_t psk_len,
                   uint8_t* out_plaintext, size_t* out_plain_len);

} // namespace meshtastic
} // namespace chat

/**
 * @file mt_packet_wire.h
 * @brief Meshtastic-compatible wire packet format
 */

#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_MSC_VER)
#define TRAILMATE_PACK_PUSH __pragma(pack(push, 1))
#define TRAILMATE_PACK_POP __pragma(pack(pop))
#define TRAILMATE_PACKED
#else
#define TRAILMATE_PACK_PUSH
#define TRAILMATE_PACK_POP
#define TRAILMATE_PACKED __attribute__((packed))
#endif

namespace chat
{
namespace meshtastic
{

TRAILMATE_PACK_PUSH
struct PacketHeaderWire
{
    uint32_t to;
    uint32_t from;
    uint32_t id;
    uint8_t flags;
    uint8_t channel;
    uint8_t next_hop;
    uint8_t relay_node;
} TRAILMATE_PACKED;
TRAILMATE_PACK_POP

constexpr uint8_t PACKET_FLAGS_HOP_LIMIT_MASK = 0x07;
constexpr uint8_t PACKET_FLAGS_WANT_ACK_MASK = 0x08;
constexpr uint8_t PACKET_FLAGS_VIA_MQTT_MASK = 0x10;
constexpr uint8_t PACKET_FLAGS_HOP_START_MASK = 0xE0;
constexpr uint8_t PACKET_FLAGS_HOP_START_SHIFT = 5;

bool buildWirePacket(const uint8_t* data_payload, size_t data_len,
                     uint32_t from_node, uint32_t packet_id,
                     uint32_t dest_node, uint8_t channel_hash,
                     uint8_t hop_limit, bool want_ack,
                     const uint8_t* psk, size_t psk_len,
                     uint8_t* out_buffer, size_t* out_size);

bool parseWirePacket(const uint8_t* buffer, size_t size,
                     PacketHeaderWire* out_header,
                     uint8_t* out_payload, size_t* out_payload_size);

bool decryptPayload(const PacketHeaderWire& header,
                    const uint8_t* cipher, size_t cipher_len,
                    const uint8_t* psk, size_t psk_len,
                    uint8_t* out_plaintext, size_t* out_plain_len);

} // namespace meshtastic
} // namespace chat

#undef TRAILMATE_PACK_PUSH
#undef TRAILMATE_PACK_POP
#undef TRAILMATE_PACKED

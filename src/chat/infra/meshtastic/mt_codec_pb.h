/**
 * @file mt_codec_pb.h
 * @brief Meshtastic protocol codec using protobuf (nanopb)
 *
 * Uses the actual Meshtastic protobuf definitions from M5Tab5-GPS
 */

#pragma once

#include "../../domain/chat_types.h"
#include <Arduino.h>
#include <stdint.h>
#include <string>

// Include generated nanopb headers from M5Tab5-GPS
// Note: pb.h must be included first, and paths are relative to generated/ directory
#include "meshtastic/channel.pb.h"
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"
#include "pb.h"
#include "pb_decode.h"
#include "pb_encode.h"

#define MESHTASTIC_PROTOBUF_AVAILABLE 1

namespace chat
{
namespace meshtastic
{

/**
 * @brief Encode text message to Meshtastic Data payload using protobuf
 * @param channel Channel ID
 * @param text Message text
 * @param from_node Source node ID
 * @param packet_id Packet ID
 * @param out_buffer Output buffer (Data message encoded)
 * @param out_size Output buffer size (updated with actual size)
 * @return true if successful
 */
bool encodeTextMessage(ChannelId channel, const std::string& text,
                       NodeId from_node, uint32_t packet_id,
                       uint8_t* out_buffer, size_t* out_size);

/**
 * @brief Decode Meshtastic Data payload to text message using protobuf
 * @param buffer Data message buffer (already decrypted)
 * @param size Buffer size
 * @param out Output message
 * @return true if successful
 */
bool decodeTextMessage(const uint8_t* buffer, size_t size, MeshIncomingText* out);

/**
 * @brief Encode node info (User) message to Meshtastic Data payload using protobuf
 * @param user_id Unique user id string (<=15 chars + null)
 * @param long_name User long name
 * @param short_name User short name (<=4 chars + null)
 * @param hw_model Hardware model enum
 * @param macaddr 6-byte MAC address
 * @param out_buffer Output buffer (Data message encoded)
 * @param out_size Output buffer size (updated with actual size)
 * @return true if successful
 */
bool encodeNodeInfoMessage(const std::string& user_id, const std::string& long_name,
                           const std::string& short_name, meshtastic_HardwareModel hw_model,
                           const uint8_t macaddr[6], const uint8_t* public_key, size_t public_key_len,
                           bool want_response, uint8_t* out_buffer, size_t* out_size);

/**
 * @brief Encode MeshPacket to buffer
 * @param packet MeshPacket structure
 * @param out_buffer Output buffer
 * @param out_size Output buffer size (updated with actual size)
 * @return true if successful
 */
bool encodeMeshPacket(const meshtastic_MeshPacket& packet, uint8_t* out_buffer, size_t* out_size);

/**
 * @brief Decode buffer to MeshPacket
 * @param buffer Packet buffer
 * @param size Packet size
 * @param out Output MeshPacket
 * @return true if successful
 */
bool decodeMeshPacket(const uint8_t* buffer, size_t size, meshtastic_MeshPacket* out);

} // namespace meshtastic
} // namespace chat

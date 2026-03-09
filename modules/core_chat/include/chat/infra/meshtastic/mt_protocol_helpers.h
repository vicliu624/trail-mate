/**
 * @file mt_protocol_helpers.h
 * @brief Shared Meshtastic protocol helper utilities
 */

#pragma once

#include "chat/domain/chat_types.h"
#include "chat/infra/meshtastic/mt_packet_wire.h"
#include "meshtastic/config.pb.h"
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"
#include "pb.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace chat
{
namespace meshtastic
{

bool shouldSetAirWantAck(uint32_t dest, bool want_ack);
const char* keyVerificationStage(const meshtastic_KeyVerification& kv);
const char* routingErrorName(meshtastic_Routing_Error err);
bool hasValidPosition(const meshtastic_Position& pos);
void expandShortPsk(uint8_t index, uint8_t* out, size_t* out_len);
bool isZeroKey(const uint8_t* key, size_t len);
uint8_t computeChannelHash(const char* name, const uint8_t* key, size_t key_len);
std::string toHex(const uint8_t* data, size_t len, size_t max_len = 64);
uint8_t computeHopsAway(uint8_t flags);
void insertTraceRouteUnknownHops(uint8_t flags,
                                 meshtastic_RouteDiscovery* route,
                                 bool towards_destination);
void appendTraceRouteNodeAndSnr(meshtastic_RouteDiscovery* route,
                                uint32_t node_id,
                                const chat::RxMeta* rx_meta,
                                bool towards_destination,
                                bool snr_only);
bool readPbString(pb_istream_t* stream, char* out, size_t out_len);
bool makeEncryptedPacketFromWire(const uint8_t* wire_data, size_t wire_size,
                                 meshtastic_MeshPacket* out_packet);
void fillDecodedPacketCommon(meshtastic_MeshPacket* packet,
                             const meshtastic_Data& decoded,
                             const PacketHeaderWire& header,
                             chat::ChannelId channel_index);
bool allowPkiForPortnum(uint32_t portnum);
uint32_t djb2HashText(const char* text);
void modemPresetToParams(meshtastic_Config_LoRaConfig_ModemPreset preset, bool wide_lora,
                         float& bw_khz, uint8_t& sf, uint8_t& cr_denom);

} // namespace meshtastic
} // namespace chat
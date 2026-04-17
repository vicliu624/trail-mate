/**
 * @file rnode_packet_wire.h
 * @brief Shared RNode over-air packet framing helpers
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace chat
{
namespace rnode
{

constexpr size_t kRNodeHeaderSize = 1;
constexpr size_t kRNodeSingleAirPacketSize = 255;
constexpr size_t kRNodeFragmentPayloadSize = kRNodeSingleAirPacketSize - kRNodeHeaderSize;
constexpr size_t kRNodeMaxPayloadSize = kRNodeFragmentPayloadSize * 2;
constexpr uint8_t kRNodeFlagSplit = 0x01;
constexpr uint8_t kRNodeSeqUnset = 0xFF;

struct ParsedAirPacket
{
    uint8_t header = 0;
    uint8_t sequence = 0;
    bool split = false;
    const uint8_t* payload = nullptr;
    size_t payload_len = 0;
};

struct EncodedAirPacketSet
{
    uint8_t first[kRNodeSingleAirPacketSize] = {};
    size_t first_len = 0;
    uint8_t second[kRNodeSingleAirPacketSize] = {};
    size_t second_len = 0;
    size_t count = 0;
    uint8_t header = 0;
};

struct ReassemblyState
{
    uint8_t sequence = kRNodeSeqUnset;
    size_t buffered_len = 0;
    uint8_t buffered[kRNodeMaxPayloadSize] = {};

    void reset()
    {
        sequence = kRNodeSeqUnset;
        buffered_len = 0;
    }
};

bool parseAirPacket(const uint8_t* data, size_t len, ParsedAirPacket* out);
bool encodeAirPacketSet(const uint8_t* payload, size_t payload_len,
                        uint8_t sequence, EncodedAirPacketSet* out);
bool feedAirPacket(ReassemblyState* state,
                   const uint8_t* data, size_t len,
                   uint8_t* out_payload, size_t* inout_payload_len,
                   bool* out_complete = nullptr);

uint32_t estimateBitrateBps(uint32_t bandwidth_hz, uint8_t spreading_factor, uint8_t coding_rate);
float estimateSymbolTimeMs(uint32_t bandwidth_hz, uint8_t spreading_factor);
uint16_t recommendPreambleSymbols(uint32_t bandwidth_hz, uint8_t spreading_factor, uint8_t coding_rate);

} // namespace rnode
} // namespace chat

/**
 * @file rnode_packet_wire.cpp
 * @brief Shared RNode over-air packet framing helpers
 */

#include "chat/infra/rnode/rnode_packet_wire.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace chat
{
namespace rnode
{

namespace
{
constexpr float kPreambleSymbolsMin = 18.0f;
constexpr float kPreambleTargetMs = 24.0f;
constexpr float kPreambleFastDeltaMs = 18.0f;
constexpr uint32_t kFastThresholdBps = 30000U;

template <typename T>
T clampValue(T value, T min_value, T max_value)
{
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

} // namespace

bool parseAirPacket(const uint8_t* data, size_t len, ParsedAirPacket* out)
{
    if (!data || len <= kRNodeHeaderSize || !out)
    {
        return false;
    }

    out->header = data[0];
    out->sequence = static_cast<uint8_t>(data[0] >> 4);
    out->split = (data[0] & kRNodeFlagSplit) != 0;
    out->payload = data + kRNodeHeaderSize;
    out->payload_len = len - kRNodeHeaderSize;
    return out->payload_len > 0;
}

bool encodeAirPacketSet(const uint8_t* payload, size_t payload_len,
                        uint8_t sequence, EncodedAirPacketSet* out)
{
    if (!payload || payload_len == 0 || payload_len > kRNodeMaxPayloadSize || !out)
    {
        return false;
    }

    const uint8_t base_header = static_cast<uint8_t>((sequence & 0x0FU) << 4);
    const bool split = payload_len > kRNodeFragmentPayloadSize;
    out->header = static_cast<uint8_t>(base_header | (split ? kRNodeFlagSplit : 0U));
    out->count = split ? 2U : 1U;

    out->first[0] = out->header;
    const size_t first_payload_len =
        split ? kRNodeFragmentPayloadSize : payload_len;
    memcpy(out->first + kRNodeHeaderSize, payload, first_payload_len);
    out->first_len = kRNodeHeaderSize + first_payload_len;

    if (split)
    {
        const size_t second_payload_len = payload_len - first_payload_len;
        out->second[0] = out->header;
        memcpy(out->second + kRNodeHeaderSize, payload + first_payload_len, second_payload_len);
        out->second_len = kRNodeHeaderSize + second_payload_len;
    }
    else
    {
        out->second_len = 0;
    }

    return true;
}

bool feedAirPacket(ReassemblyState* state,
                   const uint8_t* data, size_t len,
                   uint8_t* out_payload, size_t* inout_payload_len,
                   bool* out_complete)
{
    if (out_complete)
    {
        *out_complete = false;
    }

    if (!state || !data || !out_payload || !inout_payload_len)
    {
        return false;
    }

    ParsedAirPacket parsed{};
    if (!parseAirPacket(data, len, &parsed))
    {
        state->reset();
        return false;
    }

    auto emit_payload = [&](const uint8_t* src, size_t src_len) -> bool
    {
        if (*inout_payload_len < src_len)
        {
            *inout_payload_len = src_len;
            return false;
        }
        memcpy(out_payload, src, src_len);
        *inout_payload_len = src_len;
        if (out_complete)
        {
            *out_complete = true;
        }
        return true;
    };

    if (!parsed.split)
    {
        state->reset();
        return emit_payload(parsed.payload, parsed.payload_len);
    }

    if (state->sequence == kRNodeSeqUnset)
    {
        if (parsed.payload_len > sizeof(state->buffered))
        {
            state->reset();
            return false;
        }
        memcpy(state->buffered, parsed.payload, parsed.payload_len);
        state->buffered_len = parsed.payload_len;
        state->sequence = parsed.sequence;
        return true;
    }

    if (state->sequence != parsed.sequence)
    {
        if (parsed.payload_len > sizeof(state->buffered))
        {
            state->reset();
            return false;
        }
        memcpy(state->buffered, parsed.payload, parsed.payload_len);
        state->buffered_len = parsed.payload_len;
        state->sequence = parsed.sequence;
        return true;
    }

    if (state->buffered_len + parsed.payload_len > sizeof(state->buffered))
    {
        state->reset();
        return false;
    }

    memcpy(state->buffered + state->buffered_len, parsed.payload, parsed.payload_len);
    state->buffered_len += parsed.payload_len;

    const size_t complete_len = state->buffered_len;
    const bool ok = emit_payload(state->buffered, complete_len);
    state->reset();
    return ok;
}

uint32_t estimateBitrateBps(uint32_t bandwidth_hz, uint8_t spreading_factor, uint8_t coding_rate)
{
    if (bandwidth_hz == 0 || spreading_factor < 5 || spreading_factor > 12 ||
        coding_rate < 5 || coding_rate > 8)
    {
        return 0;
    }

    const float sf = static_cast<float>(spreading_factor);
    const float cr = static_cast<float>(coding_rate);
    const float bw_khz = static_cast<float>(bandwidth_hz) / 1000.0f;
    const float bitrate =
        sf * ((4.0f / cr) / (std::pow(2.0f, sf) / bw_khz)) * 1000.0f;
    if (!std::isfinite(bitrate) || bitrate <= 0.0f)
    {
        return 0;
    }
    return static_cast<uint32_t>(std::lround(bitrate));
}

float estimateSymbolTimeMs(uint32_t bandwidth_hz, uint8_t spreading_factor)
{
    if (bandwidth_hz == 0 || spreading_factor < 5 || spreading_factor > 12)
    {
        return 0.0f;
    }

    const float symbol_rate =
        static_cast<float>(bandwidth_hz) / std::pow(2.0f, static_cast<float>(spreading_factor));
    if (!std::isfinite(symbol_rate) || symbol_rate <= 0.0f)
    {
        return 0.0f;
    }
    return (1.0f / symbol_rate) * 1000.0f;
}

uint16_t recommendPreambleSymbols(uint32_t bandwidth_hz, uint8_t spreading_factor, uint8_t coding_rate)
{
    const float symbol_time_ms = estimateSymbolTimeMs(bandwidth_hz, spreading_factor);
    if (symbol_time_ms <= 0.0f)
    {
        return static_cast<uint16_t>(kPreambleSymbolsMin);
    }

    const uint32_t bitrate_bps = estimateBitrateBps(bandwidth_hz, spreading_factor, coding_rate);
    float target_ms = kPreambleTargetMs;
    if (bitrate_bps > kFastThresholdBps)
    {
        target_ms -= kPreambleFastDeltaMs;
    }

    float preamble_symbols = target_ms / symbol_time_ms;
    if (!std::isfinite(preamble_symbols))
    {
        preamble_symbols = kPreambleSymbolsMin;
    }

    preamble_symbols = std::max(kPreambleSymbolsMin, std::ceil(preamble_symbols));
    return clampValue<uint16_t>(static_cast<uint16_t>(preamble_symbols), 18U, 255U);
}

} // namespace rnode
} // namespace chat

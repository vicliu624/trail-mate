/*
 * LXST-derived portions: https://github.com/markqvist/LXST
 * Copyright 2024-2026, Mark Qvist, unless otherwise noted.
 * Upstream reference terms: LICENSES/LXST-CC-BY-NC-ND-4.0.txt.
 * Sharing adapted LXST material requires separate authorization from Mark Qvist.
 * This notice does not grant that authorization.
 * Any RNS/LXMF-derived portions retain their original copyright and Reticulum
 * License; see LICENSES/Reticulum-Derivatives.txt, LICENSES/Reticulum-RNS.txt
 * and LICENSES/Reticulum-LXMF.txt. Other contributions retain their own terms.
 * Component scope: docs/licensing/reticulum-sources.csv; see LICENSE.
 */

/**
 * @file audio_call_wire.cpp
 * @brief MeshChat-compatible Reticulum audio call protobuf subset.
 */

#include "chat/infra/reticulum/audio_call_wire.h"

#include <cstring>

namespace chat::reticulum::audio_call
{
namespace
{

constexpr uint8_t kWireVarint = 0;
constexpr uint8_t kWireLengthDelimited = 2;
constexpr uint32_t kFieldAudioData = 1;
constexpr uint32_t kFieldCodec2Audio = 1;
constexpr uint32_t kFieldCodec2Mode = 1;
constexpr uint32_t kFieldCodec2Encoded = 2;

std::size_t varintSize(uint64_t value)
{
    std::size_t len = 1;
    while (value >= 0x80U)
    {
        value >>= 7U;
        ++len;
    }
    return len;
}

bool writeByte(uint8_t byte, uint8_t* out, std::size_t capacity, std::size_t& used)
{
    if (!out || used >= capacity)
    {
        return false;
    }
    out[used++] = byte;
    return true;
}

bool writeVarint(uint64_t value, uint8_t* out, std::size_t capacity, std::size_t& used)
{
    do
    {
        uint8_t byte = static_cast<uint8_t>(value & 0x7FU);
        value >>= 7U;
        if (value != 0)
        {
            byte |= 0x80U;
        }
        if (!writeByte(byte, out, capacity, used))
        {
            return false;
        }
    } while (value != 0);
    return true;
}

bool writeKey(uint32_t field, uint8_t wire_type, uint8_t* out, std::size_t capacity, std::size_t& used)
{
    return writeVarint((static_cast<uint64_t>(field) << 3U) | wire_type,
                       out,
                       capacity,
                       used);
}

bool writeBytes(const uint8_t* data,
                std::size_t len,
                uint8_t* out,
                std::size_t capacity,
                std::size_t& used)
{
    if ((!data && len != 0) || !out || used > capacity || len > (capacity - used))
    {
        return false;
    }
    if (len != 0)
    {
        std::memcpy(out + used, data, len);
    }
    used += len;
    return true;
}

bool readVarint(const uint8_t* data,
                std::size_t len,
                std::size_t& offset,
                uint64_t* out)
{
    if (!data || !out)
    {
        return false;
    }

    uint64_t value = 0;
    uint8_t shift = 0;
    while (offset < len && shift < 64)
    {
        const uint8_t byte = data[offset++];
        value |= static_cast<uint64_t>(byte & 0x7FU) << shift;
        if ((byte & 0x80U) == 0)
        {
            *out = value;
            return true;
        }
        shift = static_cast<uint8_t>(shift + 7U);
    }
    return false;
}

bool skipField(uint8_t wire_type,
               const uint8_t* data,
               std::size_t len,
               std::size_t& offset)
{
    if (wire_type == kWireVarint)
    {
        uint64_t ignored = 0;
        return readVarint(data, len, offset, &ignored);
    }
    if (wire_type == kWireLengthDelimited)
    {
        uint64_t field_len = 0;
        if (!readVarint(data, len, offset, &field_len))
        {
            return false;
        }
        if (field_len > (len - offset))
        {
            return false;
        }
        offset += static_cast<std::size_t>(field_len);
        return true;
    }
    return false;
}

bool decodeCodec2Audio(const uint8_t* data,
                       std::size_t len,
                       DecodedPayload* out)
{
    if (!data || !out)
    {
        return false;
    }

    bool has_mode = false;
    bool has_encoded = false;
    std::size_t offset = 0;
    while (offset < len)
    {
        uint64_t key = 0;
        if (!readVarint(data, len, offset, &key))
        {
            return false;
        }
        const uint32_t field = static_cast<uint32_t>(key >> 3U);
        const uint8_t wire_type = static_cast<uint8_t>(key & 0x07U);

        if (field == kFieldCodec2Mode && wire_type == kWireVarint)
        {
            uint64_t mode = 0;
            if (!readVarint(data, len, offset, &mode) || mode > 8U)
            {
                return false;
            }
            out->mode = static_cast<Codec2Mode>(mode);
            has_mode = true;
        }
        else if (field == kFieldCodec2Encoded && wire_type == kWireLengthDelimited)
        {
            uint64_t encoded_len = 0;
            if (!readVarint(data, len, offset, &encoded_len) ||
                encoded_len > (len - offset))
            {
                return false;
            }
            out->encoded = data + offset;
            out->encoded_len = static_cast<std::size_t>(encoded_len);
            offset += static_cast<std::size_t>(encoded_len);
            has_encoded = true;
        }
        else if (!skipField(wire_type, data, len, offset))
        {
            return false;
        }
    }
    return has_mode && has_encoded && out->encoded != nullptr && out->encoded_len != 0;
}

bool decodeAudioData(const uint8_t* data,
                     std::size_t len,
                     DecodedPayload* out)
{
    if (!data || !out)
    {
        return false;
    }

    std::size_t offset = 0;
    while (offset < len)
    {
        uint64_t key = 0;
        if (!readVarint(data, len, offset, &key))
        {
            return false;
        }
        const uint32_t field = static_cast<uint32_t>(key >> 3U);
        const uint8_t wire_type = static_cast<uint8_t>(key & 0x07U);
        if (field == kFieldCodec2Audio && wire_type == kWireLengthDelimited)
        {
            uint64_t message_len = 0;
            if (!readVarint(data, len, offset, &message_len) ||
                message_len > (len - offset))
            {
                return false;
            }
            return decodeCodec2Audio(data + offset,
                                     static_cast<std::size_t>(message_len),
                                     out);
        }
        if (!skipField(wire_type, data, len, offset))
        {
            return false;
        }
    }
    return false;
}

} // namespace

bool encodePayload(Codec2Mode mode,
                   const uint8_t* encoded,
                   std::size_t encoded_len,
                   uint8_t* out,
                   std::size_t* inout_len)
{
    if ((!encoded && encoded_len != 0) || encoded_len == 0 || !out || !inout_len)
    {
        return false;
    }

    const std::size_t capacity = *inout_len;
    const std::size_t codec2_len =
        varintSize((kFieldCodec2Mode << 3U) | kWireVarint) +
        varintSize(static_cast<uint8_t>(mode)) +
        varintSize((kFieldCodec2Encoded << 3U) | kWireLengthDelimited) +
        varintSize(encoded_len) +
        encoded_len;
    const std::size_t audio_data_len =
        varintSize((kFieldCodec2Audio << 3U) | kWireLengthDelimited) +
        varintSize(codec2_len) +
        codec2_len;
    const std::size_t total_len =
        varintSize((kFieldAudioData << 3U) | kWireLengthDelimited) +
        varintSize(audio_data_len) +
        audio_data_len;

    if (total_len > capacity)
    {
        *inout_len = total_len;
        return false;
    }

    std::size_t used = 0;
    if (!writeKey(kFieldAudioData, kWireLengthDelimited, out, capacity, used) ||
        !writeVarint(audio_data_len, out, capacity, used) ||
        !writeKey(kFieldCodec2Audio, kWireLengthDelimited, out, capacity, used) ||
        !writeVarint(codec2_len, out, capacity, used) ||
        !writeKey(kFieldCodec2Mode, kWireVarint, out, capacity, used) ||
        !writeVarint(static_cast<uint8_t>(mode), out, capacity, used) ||
        !writeKey(kFieldCodec2Encoded, kWireLengthDelimited, out, capacity, used) ||
        !writeVarint(encoded_len, out, capacity, used) ||
        !writeBytes(encoded, encoded_len, out, capacity, used))
    {
        return false;
    }

    *inout_len = used;
    return true;
}

bool decodePayload(const uint8_t* data,
                   std::size_t len,
                   DecodedPayload* out)
{
    if (!data || len == 0 || !out)
    {
        return false;
    }

    *out = DecodedPayload{};
    std::size_t offset = 0;
    while (offset < len)
    {
        uint64_t key = 0;
        if (!readVarint(data, len, offset, &key))
        {
            return false;
        }
        const uint32_t field = static_cast<uint32_t>(key >> 3U);
        const uint8_t wire_type = static_cast<uint8_t>(key & 0x07U);
        if (field == kFieldAudioData && wire_type == kWireLengthDelimited)
        {
            uint64_t message_len = 0;
            if (!readVarint(data, len, offset, &message_len) ||
                message_len > (len - offset))
            {
                return false;
            }
            return decodeAudioData(data + offset,
                                   static_cast<std::size_t>(message_len),
                                   out);
        }
        if (!skipField(wire_type, data, len, offset))
        {
            return false;
        }
    }
    return false;
}

} // namespace chat::reticulum::audio_call

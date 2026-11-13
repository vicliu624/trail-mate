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
 * @file lxst_telephony_wire.cpp
 * @brief LXST telephony signalling and Codec2 frame MsgPack subset.
 */

#include "chat/infra/reticulum/lxst_telephony_wire.h"

#include <cstring>

namespace chat::reticulum::lxst
{
namespace
{

constexpr uint32_t kFieldSignalling = 0x00;
constexpr uint32_t kFieldFrames = 0x01;
constexpr std::size_t kMaxSkipDepth = 12;

struct Cursor
{
    const uint8_t* data = nullptr;
    std::size_t len = 0;
    std::size_t pos = 0;
};

bool appendByte(uint8_t value,
                uint8_t* out,
                std::size_t out_len,
                std::size_t& used)
{
    if (!out || used >= out_len)
    {
        return false;
    }
    out[used++] = value;
    return true;
}

bool appendBytes(const uint8_t* data,
                 std::size_t len,
                 uint8_t* out,
                 std::size_t out_len,
                 std::size_t& used)
{
    if ((!data && len != 0) || !out || used + len > out_len)
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

bool appendUint(uint32_t value,
                uint8_t* out,
                std::size_t out_len,
                std::size_t& used)
{
    if (value <= 0x7FU)
    {
        return appendByte(static_cast<uint8_t>(value), out, out_len, used);
    }
    if (value <= 0xFFU)
    {
        return appendByte(0xCC, out, out_len, used) &&
               appendByte(static_cast<uint8_t>(value), out, out_len, used);
    }
    if (value <= 0xFFFFU)
    {
        return appendByte(0xCD, out, out_len, used) &&
               appendByte(static_cast<uint8_t>(value >> 8U), out, out_len, used) &&
               appendByte(static_cast<uint8_t>(value), out, out_len, used);
    }
    return appendByte(0xCE, out, out_len, used) &&
           appendByte(static_cast<uint8_t>(value >> 24U), out, out_len, used) &&
           appendByte(static_cast<uint8_t>(value >> 16U), out, out_len, used) &&
           appendByte(static_cast<uint8_t>(value >> 8U), out, out_len, used) &&
           appendByte(static_cast<uint8_t>(value), out, out_len, used);
}

bool appendBinHeader(std::size_t len,
                     uint8_t* out,
                     std::size_t out_len,
                     std::size_t& used)
{
    if (len <= 0xFFU)
    {
        return appendByte(0xC4, out, out_len, used) &&
               appendByte(static_cast<uint8_t>(len), out, out_len, used);
    }
    if (len <= 0xFFFFU)
    {
        return appendByte(0xC5, out, out_len, used) &&
               appendByte(static_cast<uint8_t>(len >> 8U), out, out_len, used) &&
               appendByte(static_cast<uint8_t>(len), out, out_len, used);
    }
    return false;
}

bool readByte(Cursor& cursor, uint8_t* out)
{
    if (!out || cursor.pos >= cursor.len)
    {
        return false;
    }
    *out = cursor.data[cursor.pos++];
    return true;
}

bool readCount(Cursor& cursor,
               uint8_t marker,
               uint8_t fix_prefix,
               std::size_t* out_count)
{
    if (!out_count)
    {
        return false;
    }
    if ((marker & 0xF0U) == fix_prefix)
    {
        *out_count = marker & 0x0FU;
        return true;
    }
    if (marker == (fix_prefix == 0x80U ? 0xDEU : 0xDCU))
    {
        uint8_t high = 0;
        uint8_t low = 0;
        if (!readByte(cursor, &high) || !readByte(cursor, &low))
        {
            return false;
        }
        *out_count = (static_cast<std::size_t>(high) << 8U) | low;
        return true;
    }
    return false;
}

bool readMapHeader(Cursor& cursor, std::size_t* out_count)
{
    uint8_t marker = 0;
    return readByte(cursor, &marker) &&
           readCount(cursor, marker, 0x80U, out_count);
}

bool readArrayHeader(Cursor& cursor, std::size_t* out_count)
{
    uint8_t marker = 0;
    return readByte(cursor, &marker) &&
           readCount(cursor, marker, 0x90U, out_count);
}

bool readUint(Cursor& cursor, uint32_t* out)
{
    uint8_t marker = 0;
    if (!out || !readByte(cursor, &marker))
    {
        return false;
    }
    if (marker <= 0x7FU)
    {
        *out = marker;
        return true;
    }
    uint8_t bytes[4] = {};
    std::size_t count = 0;
    if (marker == 0xCCU)
    {
        count = 1;
    }
    else if (marker == 0xCDU)
    {
        count = 2;
    }
    else if (marker == 0xCEU)
    {
        count = 4;
    }
    else
    {
        return false;
    }
    uint32_t value = 0;
    for (std::size_t index = 0; index < count; ++index)
    {
        if (!readByte(cursor, &bytes[index]))
        {
            return false;
        }
        value = (value << 8U) | bytes[index];
    }
    *out = value;
    return true;
}

bool readBinView(Cursor& cursor,
                 const uint8_t** out_data,
                 std::size_t* out_len)
{
    uint8_t marker = 0;
    if (!out_data || !out_len || !readByte(cursor, &marker))
    {
        return false;
    }
    std::size_t len = 0;
    uint8_t byte = 0;
    if (marker == 0xC4U)
    {
        if (!readByte(cursor, &byte))
        {
            return false;
        }
        len = byte;
    }
    else if (marker == 0xC5U)
    {
        uint8_t high = 0;
        uint8_t low = 0;
        if (!readByte(cursor, &high) || !readByte(cursor, &low))
        {
            return false;
        }
        len = (static_cast<std::size_t>(high) << 8U) | low;
    }
    else
    {
        return false;
    }
    if (cursor.pos + len > cursor.len)
    {
        return false;
    }
    *out_data = cursor.data + cursor.pos;
    *out_len = len;
    cursor.pos += len;
    return true;
}

bool skipObject(Cursor& cursor, std::size_t depth = 0)
{
    if (depth >= kMaxSkipDepth || cursor.pos >= cursor.len)
    {
        return false;
    }
    const uint8_t marker = cursor.data[cursor.pos];
    if (marker <= 0x7FU || marker >= 0xE0U || marker == 0xC0U ||
        marker == 0xC2U || marker == 0xC3U)
    {
        ++cursor.pos;
        return true;
    }
    if (marker == 0xCCU || marker == 0xD0U)
    {
        cursor.pos += 2;
        return cursor.pos <= cursor.len;
    }
    if (marker == 0xCDU || marker == 0xD1U)
    {
        cursor.pos += 3;
        return cursor.pos <= cursor.len;
    }
    if (marker == 0xCEU || marker == 0xD2U || marker == 0xCAU)
    {
        cursor.pos += 5;
        return cursor.pos <= cursor.len;
    }
    if (marker == 0xCFU || marker == 0xD3U || marker == 0xCBU)
    {
        cursor.pos += 9;
        return cursor.pos <= cursor.len;
    }
    if (marker == 0xC4U || marker == 0xC5U)
    {
        const uint8_t* ignored = nullptr;
        std::size_t ignored_len = 0;
        return readBinView(cursor, &ignored, &ignored_len);
    }
    Cursor nested = cursor;
    std::size_t count = 0;
    if (readArrayHeader(nested, &count))
    {
        cursor = nested;
        for (std::size_t index = 0; index < count; ++index)
        {
            if (!skipObject(cursor, depth + 1))
            {
                return false;
            }
        }
        return true;
    }
    nested = cursor;
    if (readMapHeader(nested, &count))
    {
        cursor = nested;
        for (std::size_t index = 0; index < count * 2U; ++index)
        {
            if (!skipObject(cursor, depth + 1))
            {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool decodeCodec2Mode(uint8_t header,
                      audio_call::Codec2Mode* out_mode)
{
    if (!out_mode)
    {
        return false;
    }
    switch (header)
    {
    case 0x00:
        *out_mode = audio_call::Codec2Mode::Mode700C;
        return true;
    case 0x01:
        *out_mode = audio_call::Codec2Mode::Mode1200;
        return true;
    case 0x02:
        *out_mode = audio_call::Codec2Mode::Mode1300;
        return true;
    case 0x03:
        *out_mode = audio_call::Codec2Mode::Mode1400;
        return true;
    case 0x04:
        *out_mode = audio_call::Codec2Mode::Mode1600;
        return true;
    case 0x05:
        *out_mode = audio_call::Codec2Mode::Mode2400;
        return true;
    case 0x06:
        *out_mode = audio_call::Codec2Mode::Mode3200;
        return true;
    default:
        return false;
    }
}

bool encodeCodec2Mode(audio_call::Codec2Mode mode,
                      uint8_t* out_header)
{
    if (!out_header)
    {
        return false;
    }
    switch (mode)
    {
    case audio_call::Codec2Mode::Mode700C:
        *out_header = 0x00;
        return true;
    case audio_call::Codec2Mode::Mode1200:
        *out_header = 0x01;
        return true;
    case audio_call::Codec2Mode::Mode1300:
        *out_header = 0x02;
        return true;
    case audio_call::Codec2Mode::Mode1400:
        *out_header = 0x03;
        return true;
    case audio_call::Codec2Mode::Mode1600:
        *out_header = 0x04;
        return true;
    case audio_call::Codec2Mode::Mode2400:
        *out_header = 0x05;
        return true;
    case audio_call::Codec2Mode::Mode3200:
        *out_header = 0x06;
        return true;
    case audio_call::Codec2Mode::Mode450:
    case audio_call::Codec2Mode::Mode450Pwb:
        return false;
    }
    return false;
}

bool decodeAudioFrame(const uint8_t* data,
                      std::size_t len,
                      DecodedAudioFrame* out_frame)
{
    if (!data || len < 1 || !out_frame)
    {
        return false;
    }
    DecodedAudioFrame decoded{};
    decoded.codec = data[0];
    if (decoded.codec == kCodec2)
    {
        if (len < 2)
        {
            return false;
        }
        decoded.codec2_mode_valid =
            decodeCodec2Mode(data[1], &decoded.codec2_mode);
        decoded.encoded = data + 2;
        decoded.encoded_len = len - 2;
    }
    else
    {
        decoded.encoded = data + 1;
        decoded.encoded_len = len - 1;
    }
    *out_frame = decoded;
    return true;
}

} // namespace

bool profileToCodec2Mode(uint16_t profile,
                         audio_call::Codec2Mode* out_mode)
{
    if (!out_mode)
    {
        return false;
    }
    switch (profile)
    {
    case kProfileBandwidthUltraLow:
        *out_mode = audio_call::Codec2Mode::Mode700C;
        return true;
    case kProfileBandwidthVeryLow:
        *out_mode = audio_call::Codec2Mode::Mode1600;
        return true;
    case kProfileBandwidthLow:
        *out_mode = audio_call::Codec2Mode::Mode3200;
        return true;
    default:
        return false;
    }
}

bool codec2ModeToProfile(audio_call::Codec2Mode mode,
                         uint16_t* out_profile)
{
    if (!out_profile)
    {
        return false;
    }
    switch (mode)
    {
    case audio_call::Codec2Mode::Mode700C:
        *out_profile = kProfileBandwidthUltraLow;
        return true;
    case audio_call::Codec2Mode::Mode1600:
        *out_profile = kProfileBandwidthVeryLow;
        return true;
    case audio_call::Codec2Mode::Mode3200:
        *out_profile = kProfileBandwidthLow;
        return true;
    default:
        return false;
    }
}

bool encodeSignalling(uint16_t signal,
                      uint8_t* out,
                      std::size_t* inout_len)
{
    return encodeSignalling(&signal, 1, out, inout_len);
}

bool encodeSignalling(const uint16_t* signals,
                      std::size_t signal_count,
                      uint8_t* out,
                      std::size_t* inout_len)
{
    if (!signals || signal_count == 0 ||
        signal_count > DecodedPacket::kMaxSignals || !out || !inout_len)
    {
        return false;
    }
    std::size_t used = 0;
    if (!appendByte(0x81, out, *inout_len, used) ||
        !appendUint(kFieldSignalling, out, *inout_len, used) ||
        !appendByte(static_cast<uint8_t>(0x90U | signal_count),
                    out,
                    *inout_len,
                    used))
    {
        return false;
    }
    for (std::size_t index = 0; index < signal_count; ++index)
    {
        if (!appendUint(signals[index], out, *inout_len, used))
        {
            return false;
        }
    }
    *inout_len = used;
    return true;
}

bool encodeCodec2Frames(audio_call::Codec2Mode mode,
                        const uint8_t* encoded,
                        std::size_t encoded_len,
                        uint8_t* out,
                        std::size_t* inout_len)
{
    uint8_t mode_header = 0;
    if (!encoded || encoded_len == 0 || !out || !inout_len ||
        !encodeCodec2Mode(mode, &mode_header))
    {
        return false;
    }
    const std::size_t frame_len = encoded_len + 2U;
    std::size_t used = 0;
    if (!appendByte(0x81, out, *inout_len, used) ||
        !appendUint(kFieldFrames, out, *inout_len, used) ||
        !appendBinHeader(frame_len, out, *inout_len, used) ||
        !appendByte(kCodec2, out, *inout_len, used) ||
        !appendByte(mode_header, out, *inout_len, used) ||
        !appendBytes(encoded, encoded_len, out, *inout_len, used))
    {
        return false;
    }
    *inout_len = used;
    return true;
}

bool decodePacket(const uint8_t* data,
                  std::size_t len,
                  DecodedPacket* out_packet)
{
    if (!data || len == 0 || !out_packet)
    {
        return false;
    }
    *out_packet = DecodedPacket{};
    Cursor cursor{data, len, 0};
    std::size_t field_count = 0;
    if (!readMapHeader(cursor, &field_count))
    {
        return false;
    }
    for (std::size_t field_index = 0; field_index < field_count; ++field_index)
    {
        uint32_t field = 0;
        if (!readUint(cursor, &field))
        {
            return false;
        }
        if (field == kFieldSignalling)
        {
            std::size_t signal_count = 0;
            Cursor signalling_cursor = cursor;
            if (!readArrayHeader(signalling_cursor, &signal_count))
            {
                uint32_t signal = 0;
                if (!readUint(cursor, &signal))
                {
                    return false;
                }
                out_packet->signals[0] = static_cast<uint16_t>(signal);
                out_packet->signal_count = 1;
                continue;
            }
            cursor = signalling_cursor;
            for (std::size_t index = 0; index < signal_count; ++index)
            {
                uint32_t signal = 0;
                if (!readUint(cursor, &signal))
                {
                    return false;
                }
                if (out_packet->signal_count < DecodedPacket::kMaxSignals)
                {
                    out_packet->signals[out_packet->signal_count++] =
                        static_cast<uint16_t>(signal);
                }
            }
        }
        else if (field == kFieldFrames)
        {
            Cursor frame_cursor = cursor;
            const uint8_t* frame_data = nullptr;
            std::size_t frame_len = 0;
            if (readBinView(frame_cursor, &frame_data, &frame_len))
            {
                cursor = frame_cursor;
                if (out_packet->frame_count < DecodedPacket::kMaxFrames &&
                    decodeAudioFrame(frame_data,
                                     frame_len,
                                     &out_packet->frames[out_packet->frame_count]))
                {
                    ++out_packet->frame_count;
                }
                continue;
            }

            std::size_t frame_count = 0;
            if (!readArrayHeader(cursor, &frame_count))
            {
                return false;
            }
            for (std::size_t index = 0; index < frame_count; ++index)
            {
                if (!readBinView(cursor, &frame_data, &frame_len))
                {
                    return false;
                }
                if (out_packet->frame_count < DecodedPacket::kMaxFrames &&
                    decodeAudioFrame(frame_data,
                                     frame_len,
                                     &out_packet->frames[out_packet->frame_count]))
                {
                    ++out_packet->frame_count;
                }
            }
        }
        else if (!skipObject(cursor))
        {
            return false;
        }
    }
    return out_packet->signal_count != 0 || out_packet->frame_count != 0;
}

} // namespace chat::reticulum::lxst

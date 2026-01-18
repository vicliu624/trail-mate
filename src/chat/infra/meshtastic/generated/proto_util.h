/*
 * Minimal protobuf helpers for Meshtastic subset
 */
#pragma once
#include <cstdint>
#include <string>

inline void pb_put_varint(std::string& out, uint64_t v)
{
    while (true) {
        uint8_t b = v & 0x7F;
        v >>= 7;
        if (v) {
            out.push_back(b | 0x80);
        } else {
            out.push_back(b);
            break;
        }
    }
}

inline void pb_put_fixed32(std::string& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

inline bool pb_read_varint(const uint8_t* buf, size_t len, size_t& off, uint64_t& out)
{
    uint64_t v = 0;
    int shift = 0;
    while (off < len && shift < 64) {
        uint8_t b = buf[off++];
        v |= (uint64_t)(b & 0x7F) << shift;
        if ((b & 0x80) == 0) {
            out = v;
            return true;
        }
        shift += 7;
    }
    return false;
}

inline bool pb_read_fixed32(const uint8_t* buf, size_t len, size_t& off, uint32_t& out)
{
    if (off + 4 > len) return false;
    out = (uint32_t)buf[off] |
          ((uint32_t)buf[off + 1] << 8) |
          ((uint32_t)buf[off + 2] << 16) |
          ((uint32_t)buf[off + 3] << 24);
    off += 4;
    return true;
}

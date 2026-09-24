#pragma once
#include <cstddef>
#include <cstdint>

namespace sys
{
// CRC-32/ISO-HDLC. Pass a previous returned CRC to continue over another span.
// Extracted from the existing chat storage codec; default preserves its format.
inline uint32_t crc32(const void* data, std::size_t len, uint32_t previous = 0)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = ~previous;
    for (std::size_t index = 0; index < len; ++index)
    {
        crc ^= bytes[index];
        for (uint8_t bit = 0; bit < 8; ++bit)
        {
            crc = (crc & 1U) != 0U ? (crc >> 1U) ^ 0xEDB88320U : crc >> 1U;
        }
    }
    return ~crc;
}
} // namespace sys

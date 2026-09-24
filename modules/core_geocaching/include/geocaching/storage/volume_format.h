#pragma once
#include "geocaching/domain/record.h"
#include "sys/crc32.h"
#include <array>
#include <cstring>

namespace geocaching::storage
{
using VolumeInstance = std::array<uint8_t, 16>;
using VolumeHeader = std::array<uint8_t, 28>;
enum class VolumeFormatResult : uint8_t { Supported, Unsupported, Corrupt };

inline VolumeHeader encodeVolumeHeader(const VolumeInstance& instance)
{
    VolumeHeader header{};
    std::memcpy(header.data(), "TMGC", 4);
    header[5] = 1;
    header[7] = 28;
    std::memcpy(header.data() + 8, instance.data(), instance.size());
    const uint32_t crc = sys::crc32(header.data(), 24);
    for (unsigned i = 0; i < 4; ++i) header[24 + i] = static_cast<uint8_t>(crc >> ((3 - i) * 8));
    return header;
}

inline VolumeFormatResult decodeVolumeHeader(ByteView bytes, VolumeInstance& instance)
{
    instance = {};
    if (!bytes.data || bytes.size < 8 || std::memcmp(bytes.data, "TMGC", 4)) return VolumeFormatResult::Corrupt;
    // Unknown versions remain read-only; their length and checksum scheme may
    // differ, so do not interpret them using schema 1 or attempt repair.
    if (bytes.data[4] != 0 || bytes.data[5] != 1) return VolumeFormatResult::Unsupported;
    if (bytes.size != 28 || bytes.data[6] != 0 || bytes.data[7] != 28) return VolumeFormatResult::Corrupt;
    uint32_t expected = 0;
    for (unsigned i = 0; i < 4; ++i) expected = (expected << 8) | bytes.data[24 + i];
    if (expected != sys::crc32(bytes.data, 24)) return VolumeFormatResult::Corrupt;
    std::memcpy(instance.data(), bytes.data + 8, instance.size());
    return VolumeFormatResult::Supported;
}
} // namespace geocaching::storage

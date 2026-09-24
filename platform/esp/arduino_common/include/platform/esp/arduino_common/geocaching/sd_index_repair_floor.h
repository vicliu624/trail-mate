#pragma once
#include "geocaching/storage/index_root.h"
#include <cstdio>

namespace platform::esp::arduino_common::geocaching
{
// Empty flushed files carry watermarks in checked names. A torn file body
// cannot lose the watermark; older markers survive until verified recovery.
inline uint32_t indexRepairFloorCrc(const ::geocaching::storage::VolumeInstance& volume, uint64_t sequence)
{
    std::array<uint8_t, 24> bytes;
    std::memcpy(bytes.data(), volume.data(), volume.size());
    for (unsigned i = 0; i < 8; ++i) bytes[16 + i] = static_cast<uint8_t>(sequence >> ((7 - i) * 8));
    return ::sys::crc32(bytes.data(), bytes.size());
}
inline void indexRepairFloorName(const ::geocaching::storage::VolumeInstance& volume, uint64_t sequence, char (&name)[32])
{
    std::snprintf(name, sizeof(name), "gcf1-%08lx%08lx-%08lx", static_cast<unsigned long>(sequence >> 32), static_cast<unsigned long>(sequence & UINT32_MAX), static_cast<unsigned long>(indexRepairFloorCrc(volume, sequence)));
}
inline bool decodeIndexRepairFloorName(const ::geocaching::storage::VolumeInstance& volume, const char* name, uint64_t& sequence)
{
    if (std::strlen(name) != 30 || std::strncmp(name, "gcf1-", 5) || name[21] != '-') return false;
    sequence = 0;
    uint32_t crc = 0;
    for (unsigned i = 5; i < 30; ++i)
    {
        if (i == 21) continue;
        const char c = name[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        const auto value = static_cast<uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10);
        if (i < 21) sequence = (sequence << 4) | value;
        else crc = (crc << 4) | value;
    }
    return crc == indexRepairFloorCrc(volume, sequence);
}
} // namespace platform::esp::arduino_common::geocaching

#pragma once
#include "geocaching/storage/transaction_index_cursor.h"
#include "geocaching/storage/volume_format.h"

namespace geocaching::storage
{
// Disposable index metadata, not an extension of the authoritative store schema.
// One entry fits one SD slice. A transaction watermark is published separately,
// after all its entries are durable; readers ignore entries beyond that watermark.
constexpr size_t kIndexEntrySize = 152;
using IndexEntryBytes = std::array<uint8_t, kIndexEntrySize>;

inline bool validIndexEntry(const IndexedMutation& entry)
{
    const auto& value = entry.location;
    const bool checkpoint = value.source != IndexedValueSource::Journal;
    const uint32_t segment_limit = checkpoint ? UINT32_MAX : 1024U * 1024U;
    if (static_cast<uint8_t>(value.source) > 2 ||
        (checkpoint && (entry.erase || value.segment_first_sequence != value.record_sequence))) return false;
    if (entry.table < 1 || entry.table > 13 || !entry.key.data || !entry.key.size || entry.key.size > 96 ||
        !value.segment_first_sequence || value.record_sequence < value.segment_first_sequence ||
        value.frame_offset > segment_limit - 24 || value.value_size > 32768) return false;
    if (entry.erase) return !value.value_offset && !value.value_size;
    return value.value_offset >= value.frame_offset + 24 &&
           uint64_t(value.value_offset) + value.value_size <= segment_limit;
}

inline bool encodeIndexEntry(const VolumeInstance& volume, const IndexedMutation& entry, IndexEntryBytes& out)
{
    if (!validIndexEntry(entry)) return false;
    const auto input = reinterpret_cast<uintptr_t>(entry.key.data), output = reinterpret_cast<uintptr_t>(out.data());
    if (input <= output ? output - input < entry.key.size : input - output < out.size()) return false;
    out.fill(0);
    std::memcpy(out.data(), "GCI1", 4);
    std::memcpy(out.data() + 4, volume.data(), volume.size());
    out[20] = entry.table;
    out[21] = static_cast<uint8_t>(entry.key.size);
    out[22] = entry.erase ? 1 : 0;
    out[23] = static_cast<uint8_t>(entry.location.source);
    const auto put = [&](size_t offset, uint64_t value, unsigned bytes)
    { for (unsigned i = 0; i < bytes; ++i) out[offset + i] = static_cast<uint8_t>(value >> ((bytes - i - 1) * 8)); };
    put(24, entry.location.segment_first_sequence, 8);
    put(32, entry.location.record_sequence, 8);
    put(40, entry.location.frame_offset, 4);
    put(44, entry.location.value_offset, 4);
    put(48, entry.location.value_size, 4);
    std::memcpy(out.data() + 52, entry.key.data, entry.key.size);
    put(148, ::sys::crc32(out.data(), 148), 4);
    return true;
}

inline bool decodeIndexEntry(ByteView bytes, const VolumeInstance& volume, IndexedMutation& out)
{
    out = {};
    if (!bytes.data || bytes.size != kIndexEntrySize || std::memcmp(bytes.data, "GCI1", 4) ||
        std::memcmp(bytes.data + 4, volume.data(), volume.size()) || bytes.data[22] > 1 || bytes.data[23] > 2) return false;
    const auto get = [&](size_t offset, unsigned count)
    { uint64_t value = 0; for (unsigned i = 0; i < count; ++i) value = (value << 8) | bytes.data[offset + i]; return value; };
    if (get(148, 4) != ::sys::crc32(bytes.data, 148)) return false;
    IndexedMutation entry;
    entry.table = bytes.data[20];
    entry.key = {bytes.data + 52, bytes.data[21]};
    entry.erase = bytes.data[22] != 0;
    entry.location = {get(24, 8), get(32, 8), static_cast<uint32_t>(get(40, 4)), static_cast<uint32_t>(get(44, 4)), static_cast<uint32_t>(get(48, 4))};
    entry.location.source = static_cast<IndexedValueSource>(bytes.data[23]);
    if (!validIndexEntry(entry)) return false;
    for (size_t i = 52 + entry.key.size; i < 148; ++i)
        if (bytes.data[i]) return false;
    out = entry;
    return true;
}
} // namespace geocaching::storage

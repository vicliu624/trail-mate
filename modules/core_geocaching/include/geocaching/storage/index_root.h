#pragma once
#include "geocaching/storage/index_entry.h"

namespace geocaching::storage
{
constexpr size_t kIndexShardBitmapSize = 13 * 32;
using IndexRootBytes = std::array<uint8_t, 468>;
// Bitmap is borrowed from the caller's metadata lease, never copied into a view.
struct IndexRootView
{
    uint64_t epoch = 0, sequence = 0, revision = 0;
    char slot = 'a';
    ByteView shards;
};
inline bool validIndexRoot(const IndexRootView& root)
{
    if (!root.epoch || !root.revision || (root.slot != 'a' && root.slot != 'b') ||
        !root.shards.data || root.shards.size != kIndexShardBitmapSize) return false;
    if (!root.sequence)
        for (size_t i = 0; i < root.shards.size; ++i)
            if (root.shards.data[i]) return false;
    return true;
}
inline bool indexHasShard(const IndexRootView& root, uint8_t table, uint8_t bucket)
{
    return table >= 1 && table <= 13 && root.shards.data && root.shards.size == kIndexShardBitmapSize &&
           (root.shards.data[(table - 1) * 32 + bucket / 8] & (1u << (bucket % 8)));
}
inline bool encodeIndexRoot(const VolumeInstance& volume, const IndexRootView& root, IndexRootBytes& out)
{
    if (!validIndexRoot(root)) return false;
    // Permit a new header to reuse the same bitmap lease without a second copy.
    std::memmove(out.data() + 48, root.shards.data, kIndexShardBitmapSize);
    std::memset(out.data(), 0, 48);
    std::memcpy(out.data(), "GCM1", 4);
    std::memcpy(out.data() + 4, volume.data(), volume.size());
    const auto put = [&](size_t offset, uint64_t value, unsigned count)
    { for (unsigned i = 0; i < count; ++i) out[offset + i] = static_cast<uint8_t>(value >> ((count - i - 1) * 8)); };
    put(20, root.epoch, 8);
    put(28, root.sequence, 8);
    put(36, root.revision, 8);
    out[44] = static_cast<uint8_t>(root.slot);
    put(464, ::sys::crc32(out.data(), 464), 4);
    return true;
}
inline bool decodeIndexRoot(ByteView bytes, const VolumeInstance& volume, IndexRootView& out)
{
    out = {};
    if (!bytes.data || bytes.size != 468 || std::memcmp(bytes.data, "GCM1", 4) ||
        std::memcmp(bytes.data + 4, volume.data(), volume.size()) || bytes.data[45] || bytes.data[46] || bytes.data[47]) return false;
    const auto get = [&](size_t offset, unsigned count)
    { uint64_t value = 0; for (unsigned i = 0; i < count; ++i) value = (value << 8) | bytes.data[offset + i]; return value; };
    if (get(464, 4) != ::sys::crc32(bytes.data, 464)) return false;
    IndexRootView root{get(20, 8), get(28, 8), get(36, 8), static_cast<char>(bytes.data[44]), {bytes.data + 48, kIndexShardBitmapSize}};
    if (!validIndexRoot(root)) return false;
    out = root;
    return true;
}
// Both copies must be validated. Missing/corrupt metadata requires rebuilding
// the derived index, not treating an unknown set of keys as absent.
inline bool selectIndexRoot(const IndexRootView& first, const IndexRootView& second, IndexRootView& out)
{
    out = {};
    if (!validIndexRoot(first) || !validIndexRoot(second)) return false;
    if (first.revision == second.revision)
    {
        if (first.epoch != second.epoch || first.sequence != second.sequence || first.slot != second.slot ||
            std::memcmp(first.shards.data, second.shards.data, kIndexShardBitmapSize)) return false;
        out = first;
        return true;
    }
    const auto& newer = first.revision > second.revision ? first : second;
    const auto& older = first.revision > second.revision ? second : first;
    if (older.revision == UINT64_MAX || newer.revision != older.revision + 1 || newer.sequence < older.sequence) return false;
    if (newer.epoch == older.epoch)
    {
        if (newer.slot != older.slot || older.sequence == UINT64_MAX || newer.sequence != older.sequence + 1) return false;
        for (size_t i = 0; i < kIndexShardBitmapSize; ++i)
            if ((newer.shards.data[i] & older.shards.data[i]) != older.shards.data[i]) return false;
    }
    else if (newer.slot == older.slot) return false;
    out = newer;
    return true;
}
} // namespace geocaching::storage

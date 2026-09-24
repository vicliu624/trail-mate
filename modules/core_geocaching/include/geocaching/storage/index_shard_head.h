#pragma once
#include "geocaching/storage/index_entry.h"

namespace geocaching::storage
{
struct IndexShardHead
{
    uint64_t epoch = 0, sequence = 0, length = 0;
    uint8_t table = 0, bucket = 0;
};
using IndexShardHeadBytes = std::array<uint8_t, 52>;

inline bool validIndexShardHead(const IndexShardHead& head)
{
    return head.epoch && head.table >= 1 && head.table <= 13 && head.length % kIndexEntrySize == 0 &&
           ((head.sequence == 0) == (head.length == 0));
}
inline bool encodeIndexShardHead(const VolumeInstance& volume, const IndexShardHead& head, IndexShardHeadBytes& out)
{
    if (!validIndexShardHead(head)) return false;
    out.fill(0);
    std::memcpy(out.data(), "GCS1", 4);
    std::memcpy(out.data() + 4, volume.data(), volume.size());
    const auto put = [&](size_t offset, uint64_t value, unsigned count)
    { for (unsigned i = 0; i < count; ++i) out[offset + i] = static_cast<uint8_t>(value >> ((count - i - 1) * 8)); };
    put(20, head.epoch, 8);
    put(28, head.sequence, 8);
    put(36, head.length, 8);
    out[44] = head.table;
    out[45] = head.bucket;
    put(48, ::sys::crc32(out.data(), 48), 4);
    return true;
}
inline bool decodeIndexShardHead(ByteView bytes, const VolumeInstance& volume, uint64_t epoch,
                                 uint8_t table, uint8_t bucket, IndexShardHead& out)
{
    out = {};
    if (!bytes.data || bytes.size != 52 || std::memcmp(bytes.data, "GCS1", 4) ||
        std::memcmp(bytes.data + 4, volume.data(), volume.size()) || bytes.data[46] || bytes.data[47]) return false;
    const auto get = [&](size_t offset, unsigned count)
    { uint64_t value = 0; for (unsigned i = 0; i < count; ++i) value = (value << 8) | bytes.data[offset + i]; return value; };
    if (get(48, 4) != ::sys::crc32(bytes.data, 48)) return false;
    IndexShardHead head{get(20, 8), get(28, 8), get(36, 8), bytes.data[44], bytes.data[45]};
    if (!validIndexShardHead(head) || head.epoch != epoch || head.table != table || head.bucket != bucket) return false;
    out = head;
    return true;
}
// BOTH copies must already have decoded successfully. An invalid/missing copy
// might have held a committed update; silently falling back could return stale data.
// Initialize both copies to the same empty head before publishing a new shard.
inline bool selectIndexShardHead(const IndexShardHead& first, const IndexShardHead& second,
                                 uint64_t visible_sequence, IndexShardHead& out)
{
    out = {};
    if (!validIndexShardHead(first) || !validIndexShardHead(second) || first.epoch != second.epoch ||
        first.table != second.table || first.bucket != second.bucket ||
        (first.sequence == second.sequence && first.length != second.length) ||
        (first.sequence < second.sequence && first.length >= second.length) ||
        (second.sequence < first.sequence && second.length >= first.length)) return false;
    if (first.sequence > visible_sequence && second.sequence > visible_sequence) return false;
    out = first.sequence <= visible_sequence && (second.sequence > visible_sequence || first.sequence >= second.sequence) ? first : second;
    return true;
}
} // namespace geocaching::storage

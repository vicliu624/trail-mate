#pragma once
#include "geocaching/storage/index_entry.h"
#include "geocaching/storage/index_shard_head.h"
#include <cstdio>
#include <cstring>

namespace platform::esp::arduino_common::geocaching
{
inline bool indexShardPathForBucket(char slot, uint8_t table, uint8_t bucket, char* out, size_t capacity)
{
    if ((slot != 'a' && slot != 'b') || table < 1 || table > 13 || !out) return false;
    const int size = std::snprintf(out, capacity, "/trailmate/geocaching/.state/index/%c/%02x/%02x.gci", slot,
                                   static_cast<unsigned>(table), static_cast<unsigned>(bucket));
    return size > 0 && static_cast<size_t>(size) < capacity;
}
inline bool indexShardPath(char slot, uint8_t table, ::geocaching::ByteView key, char* out, size_t capacity)
{
    return key.data && key.size && key.size <= 96 &&
           indexShardPathForBucket(slot, table, static_cast<uint8_t>(::sys::crc32(key.data, key.size)), out, capacity);
}
// Current-only generations are immutable. A pending replacement must never
// truncate the file referenced by the previously committed shard head.
inline bool indexShardDataPath(char slot, const ::geocaching::storage::IndexShardHead& head, char* out, size_t capacity)
{
    if (!::geocaching::storage::validIndexShardHead(head) ||
        !indexShardPathForBucket(slot, head.table, head.bucket, out, capacity)) return false;
    if (!head.current_only) return true;
    const auto length = std::strlen(out);
    const int suffix = std::snprintf(out + length, capacity - length, ".c%016llx", static_cast<unsigned long long>(head.sequence));
    return suffix > 0 && static_cast<size_t>(suffix) < capacity - length;
}
inline bool indexShardHeadPathForBucket(char slot, uint8_t table, uint8_t bucket, unsigned copy, char* out, size_t capacity)
{
    if (copy > 1 || !indexShardPathForBucket(slot, table, bucket, out, capacity)) return false;
    const auto length = std::strlen(out);
    const int suffix = std::snprintf(out + length, capacity - length, ".h%u", copy);
    return suffix > 0 && static_cast<size_t>(suffix) < capacity - length;
}
inline bool indexShardHeadPath(char slot, uint8_t table, ::geocaching::ByteView key, unsigned copy, char* out, size_t capacity)
{
    return key.data && key.size && key.size <= 96 && indexShardHeadPathForBucket(slot, table, static_cast<uint8_t>(::sys::crc32(key.data, key.size)), copy, out, capacity);
}
} // namespace platform::esp::arduino_common::geocaching

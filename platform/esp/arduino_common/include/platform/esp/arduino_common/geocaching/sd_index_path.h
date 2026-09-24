#pragma once
#include "geocaching/storage/index_entry.h"
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

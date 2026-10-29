#pragma once
#include "track_sharing/directory_pool.h"

namespace track_sharing
{
// One bounded row per HostLink response. Caller serializes pool observation.
template <class PoolView>
inline bool encodeTrackDirectoryPageView(const PoolView& pool, size_t cursor, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!output || cursor > pool.size()) return false;
    for (size_t i = 0; i < pool.size(); ++i)
        if (!networkInputDisjoint({reinterpret_cast<const uint8_t*>(pool.at(i)), sizeof(TrackDirectoryEntry)}, output, capacity)) return false;
    geocaching::protocol::CmpWriter writer(output, capacity);
    const auto* entry = pool.at(cursor);
    if (!writer.array(2) || !(entry && cursor + 1 < pool.size() ? writer.unsignedInteger(cursor + 1) : writer.nil()) || !writer.array(entry ? 1 : 0)) return false;
    if (entry)
    {
        size_t length = 0;
        while (length < 40 && entry->name[length]) ++length;
        if (!writer.array(8) || !writer.binary({entry->delivery, 16}) || !writer.binary({entry->public_key, 64}) || !writer.binary({entry->epoch, 16}) ||
            !writer.text({entry->name, length}) || !writer.unsignedInteger(entry->capabilities_verified ? 1 : 0) ||
            !writer.unsignedInteger(entry->file_limit) || !writer.unsignedInteger(entry->chunk_limit) || !writer.unsignedInteger(entry->query_limit)) return false;
    }
    written = writer.size();
    return writer.good();
}
inline bool encodeTrackDirectoryPage(const TrackDirectoryPool& pool, size_t cursor, uint8_t* output, size_t capacity, size_t& written)
{
    return encodeTrackDirectoryPageView(pool, cursor, output, capacity, written);
}
} // namespace track_sharing

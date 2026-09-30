#pragma once
#include "geocaching/protocol/query_response.h"
#include "geocaching/storage/stored_time.h"

namespace geocaching::storage
{
struct SummaryCacheView
{
    protocol::SummaryView summary;
    ByteView snapshot_id;
    StoredTime received;
};
inline bool decodeSummaryCache(ByteView key, ByteView value, SummaryCacheView& out)
{
    out = {};
    if (!key.data || key.size != 48 || !value.data || value.size > 32768) return false;
    protocol::CmpReader reader(value);
    size_t fields = 0;
    SummaryCacheView candidate;
    if (!reader.array(fields, 3) || fields != 3 || !protocol::decodeSummary(reader, candidate.summary) ||
        std::memcmp(key.data + 16, candidate.summary.id.bytes.data(), 32) || !reader.binary(candidate.snapshot_id, 16) ||
        candidate.snapshot_id.size != 16 || !decodeStoredTime(reader, candidate.received) || !reader.finished()) return false;
    out = candidate;
    return true;
}
inline bool encodeSummaryCache(ByteView key, const protocol::SummaryView& item, ByteView snapshot_id, const StoredTime& received,
                               uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    protocol::CmpWriter writer(output, capacity);
    if (!writer.array(3) || !writer.array(11) || !writer.binary({item.id.bytes.data(), 32}) || !writer.unsignedInteger(item.revision) ||
        !writer.binary({item.hash.bytes.data(), 32}) || !writer.unsignedInteger(static_cast<uint8_t>(item.state)) ||
        !writer.signedInteger(item.latitude_e7) || !writer.signedInteger(item.longitude_e7) || !writer.text(item.name) ||
        !writer.unsignedInteger(item.difficulty_x2) || !writer.unsignedInteger(item.terrain_x2) ||
        !writer.unsignedInteger(static_cast<uint8_t>(item.container_size)) || !writer.unsignedInteger(item.signed_bytes) ||
        !writer.binary(snapshot_id) || !encodeStoredTime(writer, received)) return false;
    SummaryCacheView checked;
    if (!decodeSummaryCache(key, {output, writer.size()}, checked)) return false;
    written = writer.size();
    return true;
}
} // namespace geocaching::storage

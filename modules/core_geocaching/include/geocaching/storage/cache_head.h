#pragma once
#include "geocaching/protocol/cmp_reader.h"
#include "geocaching/protocol/cmp_writer.h"

namespace geocaching::storage
{
struct CacheHeadView
{
    ByteView current_hash;
    uint8_t conflict_state = 0;
    uint64_t install_generation = 0;
    uint32_t highest_seen_revision = 0;
};

// Table 2 structural validation. A present current_hash must also resolve to
// a verified ObjectRef for this cache before the candidate state is committed.
inline bool decodeCacheHead(ByteView key, ByteView value, CacheHeadView& out)
{
    out = {};
    if (!key.data || key.size != 32 || !value.data || value.size > 32768) return false;
    protocol::CmpReader reader(value);
    CacheHeadView candidate;
    size_t count = 0;
    uint64_t conflict = 0, highest = 0;
    if (!reader.array(count, 4) || count != 4) return false;
    auto nullable = reader;
    if (nullable.nil()) reader = nullable;
    else if (!reader.binary(candidate.current_hash, 32) || candidate.current_hash.size != 32) return false;
    if (!reader.unsignedInteger(conflict) || conflict > 2 || !reader.unsignedInteger(candidate.install_generation) ||
        candidate.install_generation == 0 || !reader.unsignedInteger(highest) || highest > UINT32_MAX ||
        !reader.finished() || (candidate.current_hash.size && highest == 0)) return false;
    candidate.conflict_state = static_cast<uint8_t>(conflict);
    candidate.highest_seen_revision = static_cast<uint32_t>(highest);
    out = candidate;
    return true;
}
inline bool encodeCacheHead(ByteView key, const CacheHeadView& head, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    protocol::CmpWriter writer(output, capacity);
    if (!writer.array(4) || !(head.current_hash.size ? writer.binary(head.current_hash) : writer.nil()) ||
        !writer.unsignedInteger(head.conflict_state) || !writer.unsignedInteger(head.install_generation) ||
        !writer.unsignedInteger(head.highest_seen_revision)) return false;
    CacheHeadView checked;
    if (!decodeCacheHead(key, {output, writer.size()}, checked)) return false;
    written = writer.size();
    return true;
}
} // namespace geocaching::storage

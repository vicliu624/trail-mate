#pragma once
#include "geocaching/storage/stored_time.h"

namespace geocaching::storage
{
struct AuthorIssuedView
{
    ByteView cache_id;
    uint32_t revision = 0;
    ByteView revision_hash;
    ByteView author_public_key;
    StoredTime issued_at;
};
inline bool decodeAuthorIssued(ByteView key, ByteView value, AuthorIssuedView& out)
{
    out = {};
    if (!key.data || key.size != 36 || !value.data || value.size > 32768) return false;
    AuthorIssuedView candidate;
    candidate.cache_id = {key.data, 32};
    for (unsigned i = 0; i < 4; ++i) candidate.revision = (candidate.revision << 8) | key.data[32 + i];
    if (!candidate.revision) return false;
    protocol::CmpReader reader(value); size_t fields = 0;
    if (!reader.array(fields, 3) || fields != 3 || !reader.binary(candidate.revision_hash, 32) || candidate.revision_hash.size != 32 ||
        !reader.binary(candidate.author_public_key, 64) || candidate.author_public_key.size != 64 ||
        !decodeStoredTime(reader, candidate.issued_at) || !reader.finished()) return false;
    out = candidate; return true;
}
inline bool encodeAuthorIssued(const RevisionHash& hash, ByteView public_key, const StoredTime& issued_at,
                                uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!public_key.data || public_key.size != 64) return false;
    protocol::CmpWriter writer(output, capacity);
    if (!writer.array(3) || !writer.binary({hash.bytes.data(), 32}) || !writer.binary(public_key) ||
        !encodeStoredTime(writer, issued_at)) return false;
    written = writer.size(); return true;
}
} // namespace geocaching::storage

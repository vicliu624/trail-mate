#pragma once
#include "geocaching/storage/draft_record.h"

namespace geocaching::storage
{
// Editor commands contain editable fields only. Restore identity metadata from
// the committed row after checking its generation. Move the encoded suffix so
// no second text buffer or borrowed previous-row data survives this call.
inline bool restoreDraftIdentity(ByteView key, const DraftView* previous, uint64_t expected,
                                 uint8_t* bytes, size_t capacity, size_t& size)
{
    DraftView next;
    if (!bytes || size > capacity || !decodeDraft(key, {bytes, size}, next) || next.author.size || next.base_hash.size ||
        expected == UINT64_MAX || next.generation != expected + 1 ||
        (previous ? previous->generation != expected : expected != 0)) return false;
    protocol::CmpReader reader({bytes, size});
    size_t fields = 0;
    if (!reader.array(fields, 4) || fields != 4 || !reader.nil() || !reader.nil()) return false;
    uint8_t prefix[101];
    protocol::CmpWriter writer(prefix, sizeof(prefix));
    if (!writer.array(4) ||
        !(previous && previous->author.size ? writer.binary(previous->author) : writer.nil()) ||
        !(previous && previous->base_hash.size ? writer.binary(previous->base_hash) : writer.nil())) return false;
    const size_t suffix = size - reader.position();
    if (writer.size() > capacity || suffix > capacity - writer.size()) return false;
    std::memmove(bytes + writer.size(), bytes + reader.position(), suffix);
    std::memcpy(bytes, prefix, writer.size());
    size = writer.size() + suffix;
    return true;
}
} // namespace geocaching::storage

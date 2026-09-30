#pragma once
#include "geocaching/protocol/cmp_writer.h"
#include "geocaching/protocol/record_decoder.h"

namespace geocaching::protocol
{
// Input views must not overlap output. This produces unsigned CacheRecord
// bytes only; signing and publishing are separate owner-controlled operations.
inline bool encodeGeocacheRecord(const RecordView& record, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if ((record.revision == 1 && record.previous_hash.size != 0) || record.name.size() > kMaxNameBytes ||
        record.description.size() > kMaxDescriptionBytes || record.hint.size() > kMaxHintBytes) return false;
    CmpWriter writer(output, capacity < kMaxRecordBytes ? capacity : kMaxRecordBytes);
    if (!writer.array(16) || !writer.unsignedInteger(1) || !writer.binary(record.author_public_key) ||
        !writer.binary(record.creation_nonce) || !writer.unsignedInteger(record.revision) ||
        !(record.revision == 1 ? writer.nil() : writer.binary(record.previous_hash)) ||
        !writer.unsignedInteger(static_cast<uint8_t>(record.state)) || !writer.signedInteger(record.latitude_e7) ||
        !writer.signedInteger(record.longitude_e7) || !writer.text(record.name) || !writer.text(record.description) ||
        !writer.text(record.hint) || !writer.unsignedInteger(record.difficulty_x2) || !writer.unsignedInteger(record.terrain_x2) ||
        !writer.unsignedInteger(static_cast<uint8_t>(record.container_size)) || !writer.unsignedInteger(record.created_at) ||
        !writer.unsignedInteger(record.updated_at)) return false;
    RecordView checked;
    if (!decodeGeocacheRecord({output, writer.size()}, checked)) return false;
    written = writer.size();
    return true;
}
} // namespace geocaching::protocol

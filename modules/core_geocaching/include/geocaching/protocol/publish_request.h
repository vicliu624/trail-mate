#pragma once
#include "geocaching/protocol/cmp_writer.h"
#include "geocaching/protocol/record_decoder.h"

namespace geocaching::protocol
{
struct PublishRequestView
{
    ByteView signed_cache;
    uint16_t budget = 0;
};
inline bool decodePublishRequest(ByteView bytes, const RequestId& expected, PublishRequestView& out)
{
    out = {};
    if (!bytes.data || bytes.size > kMaxApplicationBytes) return false;
    CmpReader reader(bytes);
    size_t fields = 0;
    uint64_t value = 0;
    ByteView id, record, signature;
    if (!reader.array(fields, 6) || fields != 6 || !reader.unsignedInteger(value) || value != 1 ||
        !reader.unsignedInteger(value) || value != 0 || !reader.unsignedInteger(value) || value != 1 ||
        !reader.binary(id, 16) || id.size != 16 || std::memcmp(id.data, expected.bytes.data(), 16) ||
        !reader.unsignedInteger(value) || value < 512 || value > kMaxApplicationBytes) return false;
    const auto budget = static_cast<uint16_t>(value);
    if (!reader.array(fields, 1) || fields != 1) return false;
    const auto offset = reader.position();
    if (!reader.array(fields, 2) || fields != 2 || !reader.binary(record, kMaxRecordBytes) || !record.size ||
        !reader.binary(signature, 64) || signature.size != 64 || !reader.finished()) return false;
    out.signed_cache = {bytes.data + offset, bytes.size - offset};
    out.budget = budget;
    return true;
}
// record/signature must already be authenticated by the caller. This function
// validates the wire shape but does not substitute for author verification.
inline bool encodePublishRequest(const RequestId& request, ByteView record,
                                 ByteView signature, std::uint16_t budget,
                                 std::uint8_t* output, std::size_t capacity,
                                 std::size_t& written)
{
    written = 0;
    RecordView parsed;
    if (budget < 512 || budget > kMaxApplicationBytes || !signature.data ||
        signature.size != 64 || !decodeGeocacheRecord(record, parsed)) return false;
    CmpWriter writer(output, capacity);
    if (!writer.array(6) || !writer.unsignedInteger(1) || !writer.unsignedInteger(0) ||
        !writer.unsignedInteger(1) || !writer.binary({request.bytes.data(), 16}) ||
        !writer.unsignedInteger(budget) || !writer.array(1) || !writer.array(2) ||
        !writer.binary(record) || !writer.binary(signature)) return false;
    written = writer.size();
    return writer.good();
}
} // namespace geocaching::protocol

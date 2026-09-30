#pragma once
#include "geocaching/protocol/cmp_reader.h"

namespace geocaching::protocol
{
struct GetRequestView
{
    ByteView cache_id, wanted_hash, known_hash;
    uint16_t budget = 0;
};
inline bool decodeGetRequest(ByteView bytes, const RequestId& expected, GetRequestView& out)
{
    out = {};
    CmpReader reader(bytes);
    size_t fields = 0;
    uint64_t value = 0;
    ByteView id;
    GetRequestView request;
    if (!reader.array(fields, 6) || fields != 6 || !reader.unsignedInteger(value) || value != 1 ||
        !reader.unsignedInteger(value) || value != 0 || !reader.unsignedInteger(value) || value != 3 ||
        !reader.binary(id, 16) || id.size != 16 || std::memcmp(id.data, expected.bytes.data(), 16) ||
        !reader.unsignedInteger(value) || value < 512 || value > 8192) return false;
    request.budget = static_cast<uint16_t>(value);
    if (!reader.array(fields, 3) || fields != 3 || !reader.binary(request.cache_id, 32) || request.cache_id.size != 32) return false;
    auto optional = reader;
    if (optional.nil()) reader = optional;
    else if (!reader.binary(request.wanted_hash, 32) || request.wanted_hash.size != 32) return false;
    optional = reader;
    if (optional.nil()) reader = optional;
    else if (!reader.binary(request.known_hash, 32) || request.known_hash.size != 32) return false;
    if (!reader.finished() || (request.wanted_hash.size && request.known_hash.size)) return false;
    out = request;
    return true;
}
} // namespace geocaching::protocol

#pragma once
#include "geocaching/protocol/record_decoder.h"
#include <cstring>

namespace geocaching::protocol
{
struct GetResponseView
{
    ByteView signed_cache;
    bool is_current = false;
    bool has_conflict = false;
};

// Accepts status 200 only. Does not authenticate source or author: the caller
// must match the transport identity, then verifyGeocache against requested IDs.
inline bool decodeGetResponse(ByteView response, const RequestId& expected,
                              std::size_t response_budget, GetResponseView& out)
{
    out = {};
    if (!response.data || response_budget < 512 || response_budget > kMaxApplicationBytes ||
        response.size > response_budget) return false;
    CmpReader reader(response);
    std::size_t count = 0;
    std::uint64_t value = 0;
    ByteView request, encoded, signature;
    if (!reader.array(count, 6) || count != 6 ||
        !reader.unsignedInteger(value) || value != 1 ||
        !reader.unsignedInteger(value) || value != 1 ||
        !reader.unsignedInteger(value) || value != 3 ||
        !reader.binary(request, 16) || request.size != 16 ||
        std::memcmp(request.data, expected.bytes.data(), 16) != 0 ||
        !reader.unsignedInteger(value) || value != 200 ||
        !reader.array(count, 3) || count != 3 ||
        !reader.array(count, 2) || count != 2 ||
        !reader.binary(encoded, kMaxRecordBytes) || encoded.size == 0 ||
        !reader.binary(signature, 64) || signature.size != 64) return false;
    GetResponseView candidate;
    // Canonical array(2) has a one-byte header; bin <=4096 has a two/three-byte
    // header. Recover the exact original slice without re-encoding signed data.
    const auto* start = encoded.data - (encoded.size <= 255 ? 3 : 4);
    candidate.signed_cache = {start, static_cast<std::size_t>(signature.data + signature.size - start)};
    if (!reader.unsignedInteger(value) || value > 1) return false;
    candidate.is_current = value != 0;
    if (!reader.unsignedInteger(value) || value > 1 || !reader.finished()) return false;
    candidate.has_conflict = value != 0;
    out = candidate;
    return true;
}
} // namespace geocaching::protocol

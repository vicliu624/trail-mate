#pragma once
#include "geocaching/protocol/cmp_reader.h"
#include <cstring>

namespace geocaching::protocol
{
enum class PublishDisposition : std::uint8_t
{
    Stored = 0,
    AlreadyPresent = 1,
};

inline bool decodePublishResponse(ByteView response, const RequestId& request,
                                  const GeocacheId& id, const RevisionHash& hash,
                                  std::uint32_t revision, CacheState state,
                                  PublishDisposition& disposition)
{
    disposition = PublishDisposition::Stored;
    if (!response.data || response.size > 512 || revision == 0 ||
        static_cast<std::uint8_t>(state) > 2) return false;
    CmpReader reader(response);
    std::size_t count = 0;
    std::uint64_t value = 0, result = 0;
    ByteView bytes;
    if (!reader.array(count, 6) || count != 6 ||
        !reader.unsignedInteger(value) || value != 1 ||
        !reader.unsignedInteger(value) || value != 1 ||
        !reader.unsignedInteger(value) || value != 1 ||
        !reader.binary(bytes, 16) || bytes.size != 16 ||
        std::memcmp(bytes.data, request.bytes.data(), 16) != 0 ||
        !reader.unsignedInteger(value) || value != 200 ||
        !reader.array(count, 7) || count != 7 ||
        !reader.binary(bytes, 32) || bytes.size != 32 ||
        std::memcmp(bytes.data, id.bytes.data(), 32) != 0 ||
        !reader.unsignedInteger(value) || value != revision ||
        !reader.binary(bytes, 32) || bytes.size != 32 ||
        std::memcmp(bytes.data, hash.bytes.data(), 32) != 0 ||
        !reader.unsignedInteger(result) || result > 1 ||
        !reader.unsignedInteger(value) || value != revision ||
        !reader.binary(bytes, 32) || bytes.size != 32 ||
        std::memcmp(bytes.data, hash.bytes.data(), 32) != 0 ||
        !reader.unsignedInteger(value) || value != static_cast<std::uint8_t>(state) ||
        !reader.finished()) return false;
    disposition = static_cast<PublishDisposition>(result);
    return true;
}
} // namespace geocaching::protocol

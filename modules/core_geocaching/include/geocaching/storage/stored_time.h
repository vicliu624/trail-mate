#pragma once
#include "geocaching/protocol/cmp_reader.h"
#include "geocaching/protocol/cmp_writer.h"

namespace geocaching::storage
{
struct StoredTime
{
    std::array<uint8_t, 16> boot_id{};
    uint64_t monotonic_ms = 0;
    uint64_t utc_seconds = 0;
    bool has_utc = false;
    bool utc_trusted = false;
};
inline bool encodeStoredTime(protocol::CmpWriter& writer, const StoredTime& time)
{
    if ((time.utc_trusted && !time.has_utc) || (time.has_utc && time.utc_seconds > 253402300799ULL)) return false;
    return writer.array(4) && writer.binary({time.boot_id.data(), 16}) && writer.unsignedInteger(time.monotonic_ms) &&
           (time.has_utc ? writer.unsignedInteger(time.utc_seconds) : writer.nil()) && writer.unsignedInteger(time.utc_trusted ? 1 : 0);
}
inline bool decodeStoredTime(protocol::CmpReader& reader, StoredTime& out)
{
    out = {};
    StoredTime candidate;
    ByteView boot;
    size_t fields = 0;
    uint64_t trusted = 0;
    if (!reader.array(fields, 4) || fields != 4 || !reader.binary(boot, 16) || boot.size != 16 ||
        !reader.unsignedInteger(candidate.monotonic_ms)) return false;
    std::memcpy(candidate.boot_id.data(), boot.data, 16);
    auto nullable = reader;
    if (nullable.nil()) reader = nullable;
    else
    {
        if (!reader.unsignedInteger(candidate.utc_seconds) || candidate.utc_seconds > 253402300799ULL) return false;
        candidate.has_utc = true;
    }
    if (!reader.unsignedInteger(trusted) || trusted > 1 || (trusted && !candidate.has_utc)) return false;
    candidate.utc_trusted = trusted != 0;
    out = candidate;
    return true;
}
} // namespace geocaching::storage

#pragma once
#include "geocaching/protocol/record_decoder.h"
#include <cstring>

namespace geocaching::protocol
{
struct DirectoryCapabilities
{
    std::uint8_t max_query_items = 0;
    std::uint32_t cursor_ttl_seconds = 0;
    std::string_view name;
};

// This entry point accepts public directory capabilities only. The transport
// owner must first authenticate the source against the pending request target.
inline bool decodeDirectoryCapabilities(ByteView response, const RequestId& expected,
                                        DirectoryCapabilities& out)
{
    out = {};
    if (!response.data || response.size > 512) return false;
    CmpReader reader(response);
    std::size_t count = 0;
    std::uint64_t value = 0;
    ByteView request;
    if (!reader.array(count, 6) || count != 6 ||
        !reader.unsignedInteger(value) || value != 1 ||
        !reader.unsignedInteger(value) || value != 1 ||
        !reader.unsignedInteger(value) || value != 0 ||
        !reader.binary(request, 16) || request.size != 16 ||
        std::memcmp(request.data, expected.bytes.data(), 16) != 0 ||
        !reader.unsignedInteger(value) || value != 200 ||
        !reader.array(count, 12) || count != 12) return false;
    for (unsigned list = 0; list < 2; ++list)
    {
        if (!reader.array(count, 8) || count == 0) return false;
        bool supports_one = false;
        std::uint64_t previous = 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (!reader.unsignedInteger(value) || value == 0 || (i && value <= previous)) return false;
            supports_one = supports_one || value == 1;
            previous = value;
        }
        if (!supports_one) return false;
    }
    if (!reader.array(count, 5) || count != 5) return false;
    for (std::uint64_t i = 0; i < 5; ++i)
        if (!reader.unsignedInteger(value) || value != i) return false;
    if (!reader.unsignedInteger(value) || value != kMaxApplicationBytes ||
        !reader.unsignedInteger(value) || value != kMaxRecordBytes) return false;
    DirectoryCapabilities candidate;
    if (!reader.unsignedInteger(value) || value < 1 || value > 64) return false;
    candidate.max_query_items = static_cast<std::uint8_t>(value);
    if (!reader.unsignedInteger(value) || value < 60 || value > 604800) return false;
    candidate.cursor_ttl_seconds = static_cast<std::uint32_t>(value);
    if (!reader.unsignedInteger(value) || value != 2592000 ||
        !reader.unsignedInteger(value) || value != 2 ||
        !reader.text(candidate.name, 96) || !validRecordText(candidate.name, false, false) ||
        !reader.unsignedInteger(value) || value != 1 ||
        !reader.unsignedInteger(value) || value != 2592000 || !reader.finished()) return false;
    out = candidate;
    return true;
}
} // namespace geocaching::protocol

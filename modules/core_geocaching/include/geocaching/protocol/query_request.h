#pragma once
#include "geocaching/protocol/cmp_reader.h"
#include "geocaching/protocol/cmp_writer.h"

namespace geocaching::protocol
{
struct QueryRegion
{
    std::int32_t south_e7 = 0;
    std::int32_t west_e7 = 0;
    std::int32_t north_e7 = 0;
    std::int32_t east_e7 = 0;
};

struct QueryRequestView
{
    QueryRegion region;
    ByteView author_hash, cursor;
    uint16_t budget = 0;
    uint8_t state_mask = 0, page_limit = 0;
};

inline bool decodeQueryRequest(ByteView bytes, const RequestId& expected, QueryRequestView& out)
{
    out = {};
    CmpReader reader(bytes);
    size_t fields = 0;
    uint64_t value = 0;
    int64_t coordinate = 0;
    ByteView id;
    QueryRequestView request;
    if (!reader.array(fields, 6) || fields != 6 || !reader.unsignedInteger(value) || value != 1 ||
        !reader.unsignedInteger(value) || value != 0 || !reader.unsignedInteger(value) || value != 2 ||
        !reader.binary(id, 16) || id.size != 16 || std::memcmp(id.data, expected.bytes.data(), 16) ||
        !reader.unsignedInteger(value) || value < 512 || value > 8192) return false;
    request.budget = static_cast<uint16_t>(value);
    if (!reader.array(fields, 5) || fields != 5 || !reader.array(fields, 4) || fields != 4) return false;
    int32_t* coordinates[] = {&request.region.south_e7, &request.region.west_e7, &request.region.north_e7, &request.region.east_e7};
    for (auto* component : coordinates)
    {
        if (!reader.signedInteger(coordinate) || coordinate < INT32_MIN || coordinate > INT32_MAX) return false;
        *component = static_cast<int32_t>(coordinate);
    }
    const auto& region = request.region;
    if (region.south_e7 < -900000000 || region.north_e7 > 900000000 || region.south_e7 > region.north_e7 ||
        region.west_e7 < -1800000000 || region.east_e7 > 1800000000 || region.west_e7 > region.east_e7 ||
        !reader.unsignedInteger(value) || value < 1 || value > 7) return false;
    request.state_mask = static_cast<uint8_t>(value);
    auto nullable = reader;
    if (nullable.nil()) reader = nullable;
    else if (!reader.binary(request.author_hash, 32) || request.author_hash.size != 32) return false;
    if (!reader.unsignedInteger(value) || value == 0 || value > 64) return false;
    request.page_limit = static_cast<uint8_t>(value);
    nullable = reader;
    if (nullable.nil()) reader = nullable;
    else if (!reader.binary(request.cursor, 64) || !request.cursor.size) return false;
    if (!reader.finished()) return false;
    out = request;
    return true;
}

inline bool encodeCapabilitiesRequest(const RequestId& request, std::uint8_t* output,
                                      std::size_t capacity, std::size_t& written)
{
    written = 0;
    CmpWriter writer(output, capacity);
    if (!writer.array(6) || !writer.unsignedInteger(1) || !writer.unsignedInteger(0) ||
        !writer.unsignedInteger(0) || !writer.binary({request.bytes.data(), 16}) ||
        !writer.unsignedInteger(8192) || !writer.array(0)) return false;
    written = writer.size();
    return writer.good();
}

inline bool encodeQueryRequest(const RequestId& request, const QueryRegion& region,
                               std::uint8_t state_mask, ByteView author_hash,
                               std::uint8_t page_limit, ByteView cursor,
                               std::uint16_t budget, std::uint8_t* output,
                               std::size_t capacity, std::size_t& written)
{
    written = 0;
    if (region.south_e7 < -900000000 || region.north_e7 > 900000000 ||
        region.south_e7 > region.north_e7 || region.west_e7 < -1800000000 ||
        region.east_e7 > 1800000000 || region.west_e7 > region.east_e7 ||
        state_mask == 0 || state_mask > 7 || page_limit == 0 || page_limit > 64 ||
        budget < 512 || budget > kMaxApplicationBytes ||
        (author_hash.size != 0 && (author_hash.size != 32 || !author_hash.data)) ||
        cursor.size > 64 || (cursor.size && !cursor.data)) return false;
    CmpWriter writer(output, capacity);
    if (!writer.array(6) || !writer.unsignedInteger(1) || !writer.unsignedInteger(0) ||
        !writer.unsignedInteger(2) || !writer.binary({request.bytes.data(), 16}) ||
        !writer.unsignedInteger(budget) || !writer.array(5) || !writer.array(4) ||
        !writer.signedInteger(region.south_e7) || !writer.signedInteger(region.west_e7) ||
        !writer.signedInteger(region.north_e7) || !writer.signedInteger(region.east_e7) ||
        !writer.unsignedInteger(state_mask) ||
        !(author_hash.size ? writer.binary(author_hash) : writer.nil()) ||
        !writer.unsignedInteger(page_limit) ||
        !(cursor.size ? writer.binary(cursor) : writer.nil())) return false;
    written = writer.size();
    return writer.good();
}
} // namespace geocaching::protocol

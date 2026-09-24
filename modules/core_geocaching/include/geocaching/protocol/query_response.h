#pragma once
#include "geocaching/protocol/record_decoder.h"
#include <cstring>

namespace geocaching::protocol
{
struct SummaryView
{
    GeocacheId id;
    RevisionHash hash;
    std::uint32_t revision = 0;
    CacheState state = CacheState::Active;
    std::int32_t latitude_e7 = 0;
    std::int32_t longitude_e7 = 0;
    std::string_view name;
    std::uint8_t difficulty_x2 = 0;
    std::uint8_t terrain_x2 = 0;
    ContainerSize container_size = ContainerSize::Unspecified;
    std::uint16_t signed_bytes = 0;
};

struct QueryPageView
{
    ByteView snapshot_id;
    ByteView next_cursor;
    std::uint32_t remaining_ttl = 0;
    std::size_t count = 0;
    ByteView encoded_items;
};

// Shared summary parser for wire pages and the local summary cache.
inline bool decodeSummary(CmpReader& r, SummaryView& out)
{
    out = {};
    SummaryView item;
    size_t count = 0;
    uint64_t value = 0;
    int64_t coordinate = 0;
    ByteView bytes;
    if (!r.array(count, 11) || count != 11 || !r.binary(bytes, 32) || bytes.size != 32) return false;
    std::memcpy(item.id.bytes.data(), bytes.data, 32);
    if (!r.unsignedInteger(value) || value == 0 || value > UINT32_MAX) return false;
    item.revision = static_cast<uint32_t>(value);
    if (!r.binary(bytes, 32) || bytes.size != 32) return false;
    std::memcpy(item.hash.bytes.data(), bytes.data, 32);
    if (!r.unsignedInteger(value) || value > 2) return false;
    item.state = static_cast<CacheState>(value);
    if (!r.signedInteger(coordinate) || coordinate < -900000000 || coordinate > 900000000) return false;
    item.latitude_e7 = static_cast<int32_t>(coordinate);
    if (!r.signedInteger(coordinate) || coordinate < -1800000000 || coordinate >= 1800000000) return false;
    item.longitude_e7 = static_cast<int32_t>(coordinate);
    if (!r.text(item.name, kMaxNameBytes) || !validRecordText(item.name, false, true) ||
        !r.unsignedInteger(value) || value < 2 || value > 10) return false;
    item.difficulty_x2 = static_cast<uint8_t>(value);
    if (!r.unsignedInteger(value) || value < 2 || value > 10) return false;
    item.terrain_x2 = static_cast<uint8_t>(value);
    if (!r.unsignedInteger(value) || value > 5) return false;
    item.container_size = static_cast<ContainerSize>(value);
    if (!r.unsignedInteger(value) || value == 0 || value > 4166) return false;
    item.signed_bytes = static_cast<uint16_t>(value);
    out = item;
    return true;
}

// Validate the complete response with one transient summary. Returned spans
// borrow response bytes; no summary array or payload copy is retained.
inline bool decodeQueryPage(ByteView response, const RequestId& expected,
                            std::size_t budget, std::size_t capacity, QueryPageView& out)
{
    out = {};
    if (!response.data || budget < 512 || budget > kMaxApplicationBytes || response.size > budget) return false;
    CmpReader r(response);
    std::uint64_t value = 0;
    std::size_t count = 0;
    ByteView bytes;
    QueryPageView page;
    if (!r.array(count, 6) || count != 6 || !r.unsignedInteger(value) || value != 1 ||
        !r.unsignedInteger(value) || value != 1 || !r.unsignedInteger(value) || value != 2 ||
        !r.binary(bytes, 16) || bytes.size != 16 || std::memcmp(bytes.data, expected.bytes.data(), 16) != 0 ||
        !r.unsignedInteger(value) || value != 200 || !r.array(count, 4) || count != 4 ||
        !r.binary(page.snapshot_id, 16) || page.snapshot_id.size != 16 || !r.array(page.count, 64) ||
        page.count > capacity) return false;
    const auto start = r.position();
    GeocacheId previous;
    SummaryView item;
    for (std::size_t i = 0; i < page.count; ++i)
    {
        if (!decodeSummary(r, item) || (i && !(previous.bytes < item.id.bytes))) return false;
        previous = item.id;
    }
    page.encoded_items = {response.data + start, r.position() - start};
    auto probe = r;
    if (probe.nil()) r = probe;
    else if (!r.binary(page.next_cursor, 64) || page.next_cursor.size == 0 || page.count == 0) return false;
    if (!r.unsignedInteger(value) || value > 604800 || !r.finished()) return false;
    page.remaining_ttl = static_cast<std::uint32_t>(value);
    out = page;
    return true;
}
// Contiguous summary callers share the same validation and single-item parser.
inline bool decodeQueryResponse(ByteView response, const RequestId& expected,
                                std::size_t budget, SummaryView* items,
                                std::size_t capacity, QueryPageView& out)
{
    out = {};
    QueryPageView page;
    if (!decodeQueryPage(response, expected, budget, capacity, page) || (page.count && !items)) return false;
    CmpReader reader(page.encoded_items);
    for (size_t i = 0; i < page.count; ++i)
        if (!decodeSummary(reader, items[i])) return false;
    out = page;
    return true;
}
} // namespace geocaching::protocol

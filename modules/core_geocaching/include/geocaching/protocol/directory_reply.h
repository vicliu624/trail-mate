#pragma once
#include "geocaching/protocol/capabilities.h"
#include "geocaching/protocol/query_request.h"
#include "geocaching/protocol/query_response.h"

namespace geocaching::protocol
{
inline bool matchesCapabilitiesRequest(ByteView encoded, const RequestId& expected, size_t response_size)
{
    CmpReader request(encoded);
    size_t fields = 0;
    uint64_t version = 0, type = 0, operation = 0, budget = 0;
    ByteView id;
    return request.array(fields, 6) && fields == 6 && request.unsignedInteger(version) && version == 1 &&
           request.unsignedInteger(type) && type == 0 && request.unsignedInteger(operation) && operation == 0 &&
           request.binary(id, 16) && id.size == 16 && !std::memcmp(id.data, expected.bytes.data(), 16) &&
           request.unsignedInteger(budget) && budget >= 512 && budget <= kMaxApplicationBytes && budget >= response_size &&
           request.array(fields, 0) && fields == 0 && request.finished();
}

// Shape, request identity, page budget and every summary's filter membership.
// Transport authentication and durable acceptance remain owner responsibilities.
inline bool matchesQueryReply(ByteView request, ByteView response, const RequestId& id)
{
    QueryRequestView wanted;
    QueryPageView page;
    if (!decodeQueryRequest(request, id, wanted) ||
        !decodeQueryPage(response, id, wanted.budget, wanted.page_limit, page)) return false;
    CmpReader rows(page.encoded_items);
    SummaryView item;
    for (size_t i = 0; i < page.count; ++i)
        if (!decodeSummary(rows, item) || !(wanted.state_mask & (1u << static_cast<uint8_t>(item.state))) ||
            item.latitude_e7 < wanted.region.south_e7 || item.latitude_e7 > wanted.region.north_e7 ||
            item.longitude_e7 < wanted.region.west_e7 || item.longitude_e7 > wanted.region.east_e7) return false;
    return rows.finished();
}
} // namespace geocaching::protocol

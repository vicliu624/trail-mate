#include "geocaching/protocol/query_response.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv)
{
    assert(argc == 2);
    std::ifstream f(argv[1], std::ios::binary);
    assert(f.good());
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    geocaching::RequestId id;
    id.bytes.fill(3);
    std::vector<geocaching::protocol::SummaryView> items(1);
    geocaching::protocol::QueryPageView page;
    using geocaching::protocol::decodeQueryResponse;
    assert(decodeQueryResponse({bytes.data(), bytes.size()}, id, 2048, items.data(), 1, page));
    assert(page.count == 1 && page.next_cursor.size == 0 && page.remaining_ttl == 604800);
    assert(items[0].name == "Test" && items[0].latitude_e7 == 302500000 && items[0].signed_bytes == 190);
    assert(!decodeQueryResponse({bytes.data(), bytes.size()}, id, 2048, nullptr, 0, page));
    assert(page.count == 0);
    for (std::size_t n = 0; n < bytes.size(); ++n)
        assert(!decodeQueryResponse({bytes.data(), n}, id, 2048, items.data(), 1, page));
    bytes.push_back(0);
    assert(!decodeQueryResponse({bytes.data(), bytes.size()}, id, 2048, items.data(), 1, page));
}

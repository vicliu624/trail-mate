#include "geocaching/protocol/query_request.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv)
{
    assert(argc == 3);
    using namespace geocaching::protocol;
    std::ifstream cap_file(argv[1], std::ios::binary), query_file(argv[2], std::ios::binary);
    assert(cap_file.good() && query_file.good());
    std::vector<std::uint8_t> cap((std::istreambuf_iterator<char>(cap_file)), {});
    std::vector<std::uint8_t> query((std::istreambuf_iterator<char>(query_file)), {});
    std::vector<std::uint8_t> output(256);
    geocaching::RequestId id;
    id.bytes.fill(1);
    std::size_t size = 0;
    assert(encodeCapabilitiesRequest(id, output.data(), output.size(), size));
    assert(size == cap.size() && std::memcmp(output.data(), cap.data(), size) == 0);
    id.bytes.fill(3);
    QueryRegion region{300000000, 1200000000, 310000000, 1210000000};
    assert(encodeQueryRequest(id, region, 3, {}, 20, {}, 2048, output.data(), output.size(), size));
    assert(size == query.size() && std::memcmp(output.data(), query.data(), size) == 0);
    for (std::size_t n = 0; n < query.size(); ++n)
    {
        assert(!encodeQueryRequest(id, region, 3, {}, 20, {}, 2048, output.data(), n, size));
        assert(size == 0);
    }
    region.south_e7 = region.north_e7 + 1;
    assert(!encodeQueryRequest(id, region, 3, {}, 20, {}, 2048, output.data(), output.size(), size));
    region = {-900000000, -1800000000, 900000000, 1800000000};
    assert(encodeQueryRequest(id, region, 7, {}, 64, {}, 8192, output.data(), output.size(), size));
    assert(!encodeQueryRequest(id, region, 0, {}, 20, {}, 2048, output.data(), output.size(), size));
    assert(!encodeQueryRequest(id, region, 3, {nullptr, 32}, 20, {}, 2048, output.data(), output.size(), size));
}

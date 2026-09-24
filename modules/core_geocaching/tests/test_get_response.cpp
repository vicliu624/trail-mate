#include "geocaching/protocol/get_response.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv)
{
    assert(argc == 3);
    std::ifstream response_file(argv[1], std::ios::binary), signed_file(argv[2], std::ios::binary);
    assert(response_file.good() && signed_file.good());
    std::vector<std::uint8_t> response((std::istreambuf_iterator<char>(response_file)), {});
    std::vector<std::uint8_t> signed_cache((std::istreambuf_iterator<char>(signed_file)), {});
    geocaching::RequestId id;
    id.bytes.fill(4);
    geocaching::protocol::GetResponseView out;
    using geocaching::protocol::decodeGetResponse;
    assert(decodeGetResponse({response.data(), response.size()}, id, 8192, out));
    assert(out.is_current && !out.has_conflict);
    assert(out.signed_cache.size == signed_cache.size());
    assert(std::memcmp(out.signed_cache.data, signed_cache.data(), signed_cache.size()) == 0);
    for (std::size_t n = 0; n < response.size(); ++n)
    {
        assert(!decodeGetResponse({response.data(), n}, id, 8192, out));
        assert(out.signed_cache.data == nullptr);
    }
    response.back() = 2;
    assert(!decodeGetResponse({response.data(), response.size()}, id, 8192, out));
    response.back() = 0;
    id.bytes[0] = 5;
    assert(!decodeGetResponse({response.data(), response.size()}, id, 8192, out));
}

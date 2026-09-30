#include "geocaching/protocol/capabilities.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv)
{
    assert(argc == 2);
    std::ifstream file(argv[1], std::ios::binary);
    assert(file.good());
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    geocaching::RequestId request;
    request.bytes.fill(1);
    geocaching::protocol::DirectoryCapabilities result;
    using geocaching::protocol::decodeDirectoryCapabilities;
    assert(decodeDirectoryCapabilities({bytes.data(), bytes.size()}, request, result));
    assert(result.max_query_items == 64 && result.cursor_ttl_seconds == 604800);
    assert(result.name == "Public test directory");
    for (std::size_t n = 0; n < bytes.size(); ++n)
    {
        assert(!decodeDirectoryCapabilities({bytes.data(), n}, request, result));
        assert(result.name.empty() && result.max_query_items == 0);
    }
    request.bytes[0] = 2;
    assert(!decodeDirectoryCapabilities({bytes.data(), bytes.size()}, request, result));
    request.bytes.fill(1);
    // Public role is the byte preceding the uint32 sync TTL.
    bytes[bytes.size() - 6] = 0;
    assert(!decodeDirectoryCapabilities({bytes.data(), bytes.size()}, request, result));
}

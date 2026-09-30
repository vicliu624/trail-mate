#include "geocaching/protocol/cmp_writer.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv)
{
    assert(argc == 2);
    std::ifstream file(argv[1], std::ios::binary);
    assert(file.good());
    std::vector<std::uint8_t> fixture((std::istreambuf_iterator<char>(file)), {});
    assert(fixture.size() == 95);
    geocaching::RequestId request;
    geocaching::GeocacheId id;
    geocaching::RevisionHash hash;
    std::memcpy(request.bytes.data(), fixture.data() + 6, 16);
    std::memcpy(id.bytes.data(), fixture.data() + 28, 32);
    std::memcpy(hash.bytes.data(), fixture.data() + 62, 32);
    std::vector<std::uint8_t> output(128);
    std::size_t written = 99;
    using geocaching::protocol::encodeGetRequest;
    assert(encodeGetRequest(request, id, &hash, nullptr, 8192, output.data(), output.size(), written));
    assert(written == fixture.size() && std::memcmp(output.data(), fixture.data(), written) == 0);
    for (std::size_t n = 0; n < fixture.size(); ++n)
    {
        assert(!encodeGetRequest(request, id, &hash, nullptr, 8192, output.data(), n, written));
        assert(written == 0);
    }
    assert(!encodeGetRequest(request, id, &hash, &hash, 8192, output.data(), output.size(), written));
}

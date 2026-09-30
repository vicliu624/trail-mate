#include "geocaching/protocol/publish_request.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv)
{
    assert(argc == 3);
    std::ifstream signed_file(argv[1], std::ios::binary), request_file(argv[2], std::ios::binary);
    assert(signed_file.good() && request_file.good());
    std::vector<std::uint8_t> signed_bytes((std::istreambuf_iterator<char>(signed_file)), {});
    std::vector<std::uint8_t> expected((std::istreambuf_iterator<char>(request_file)), {});
    geocaching::protocol::CmpReader reader({signed_bytes.data(), signed_bytes.size()});
    std::size_t count = 0;
    geocaching::ByteView record, signature;
    assert(reader.array(count, 2) && reader.binary(record, 4096) && reader.binary(signature, 64));
    geocaching::RequestId id;
    id.bytes.fill(2);
    std::vector<std::uint8_t> output(512);
    std::size_t size = 0;
    using geocaching::protocol::encodePublishRequest;
    assert(encodePublishRequest(id, record, signature, 512, output.data(), output.size(), size));
    assert(size == expected.size() && std::memcmp(output.data(), expected.data(), size) == 0);
    for (std::size_t n = 0; n < expected.size(); ++n)
    {
        assert(!encodePublishRequest(id, record, signature, 512, output.data(), n, size));
        assert(size == 0);
    }
    assert(!encodePublishRequest(id, record, {signature.data, 63}, 512, output.data(), output.size(), size));
    assert(!encodePublishRequest(id, record, signature, 511, output.data(), output.size(), size));
    assert(!encodePublishRequest(id, {record.data, record.size - 1}, signature, 512, output.data(), output.size(), size));
}

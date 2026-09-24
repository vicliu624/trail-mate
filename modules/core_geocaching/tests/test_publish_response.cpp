#include "geocaching/protocol/publish_response.h"
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
    assert(bytes.size() == 131);
    geocaching::RequestId request;
    request.bytes.fill(2);
    geocaching::GeocacheId id;
    geocaching::RevisionHash hash;
    // Fixed external fixture layout: cache ID begins at 27, hash at 62.
    std::memcpy(id.bytes.data(), bytes.data() + 27, 32);
    std::memcpy(hash.bytes.data(), bytes.data() + 62, 32);
    using namespace geocaching::protocol;
    PublishDisposition disposition;
    assert(decodePublishResponse({bytes.data(), bytes.size()}, request, id, hash, 1,
                                 geocaching::CacheState::Active, disposition));
    assert(disposition == PublishDisposition::Stored);
    assert(!decodePublishResponse({bytes.data(), bytes.size()}, request, id, hash, 2,
                                  geocaching::CacheState::Active, disposition));
    hash.bytes[0] ^= 1;
    assert(!decodePublishResponse({bytes.data(), bytes.size()}, request, id, hash, 1,
                                  geocaching::CacheState::Active, disposition));
    hash.bytes[0] ^= 1;
    for (std::size_t n = 0; n < bytes.size(); ++n)
        assert(!decodePublishResponse({bytes.data(), n}, request, id, hash, 1,
                                      geocaching::CacheState::Active, disposition));
}

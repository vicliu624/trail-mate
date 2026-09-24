#include "geocaching/protocol/discovery.h"
#include <cassert>
#include <vector>

int main()
{
    // [1, bin16(0x11...), bin16(0x22...), uint64_max, "Node"]
    std::vector<std::uint8_t> data{0x95, 1, 0xc4, 16};
    data.insert(data.end(), 16, 0x11);
    data.insert(data.end(), {0xc4, 16});
    data.insert(data.end(), 16, 0x22);
    data.push_back(0xcf);
    data.insert(data.end(), 8, 0xff);
    data.insert(data.end(), {0xa4, 'N', 'o', 'd', 'e'});
    geocaching::Destination expected;
    expected.bytes.fill(0x11);
    geocaching::protocol::DirectoryAnnouncement out;
    using geocaching::protocol::decodeDirectoryAnnouncement;
    assert(decodeDirectoryAnnouncement({data.data(), data.size()}, expected, out));
    assert(out.sequence == UINT64_MAX && out.epoch[0] == 0x22 && out.name == "Node");
    for (std::size_t n = 0; n < data.size(); ++n)
    {
        assert(!decodeDirectoryAnnouncement({data.data(), n}, expected, out));
        assert(out.name.empty() && out.sequence == 0);
    }
    expected.bytes[0] = 0x33;
    assert(!decodeDirectoryAnnouncement({data.data(), data.size()}, expected, out));
    expected.bytes.fill(0x11);
    data.push_back(0);
    assert(!decodeDirectoryAnnouncement({data.data(), data.size()}, expected, out));
}

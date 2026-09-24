#include "geocaching/usecase/query_client.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

struct PagingPort : geocaching::QueryClientPort
{
    unsigned sends = 0, commits = 0;
    bool fail_commit = false;
    std::vector<std::uint8_t> sent;
    bool newRequestId(geocaching::RequestId& id) override
    {
        id.bytes.fill(sends == 0 ? 1 : sends == 1 ? 3
                                                  : 4);
        return true;
    }
    geocaching::QueryPersistence submit(const geocaching::DirectoryEntry&, const geocaching::RequestId&, geocaching::ByteView bytes) override
    {
        sent.assign(bytes.data, bytes.data + bytes.size);
        ++sends;
        return geocaching::QueryPersistence::Committed;
    }
    geocaching::QueryPersistence pollPersistence() override { return geocaching::QueryPersistence::Committed; }
    geocaching::QueryPersistence cancel(const geocaching::Destination&, const geocaching::RequestId&) override { return geocaching::QueryPersistence::Committed; }
    geocaching::QueryPersistence commitCapabilities(const geocaching::Destination&, const geocaching::RequestId&, geocaching::ByteView) override { return geocaching::QueryPersistence::Committed; }
    geocaching::QueryPersistence commitPage(const geocaching::Destination&, const geocaching::RequestId&, geocaching::ByteView,
                    const geocaching::protocol::QueryPageView&) override
    {
        if (fail_commit) return geocaching::QueryPersistence::Rejected;
        ++commits;
        return geocaching::QueryPersistence::Committed;
    }
};
int main(int argc, char** argv)
{
    assert(argc == 4);
    std::vector<std::uint8_t> fixture[3];
    for (unsigned i = 0; i < 3; ++i)
    {
        std::ifstream f(argv[i + 1], std::ios::binary);
        assert(f.good());
        fixture[i].assign(std::istreambuf_iterator<char>(f), {});
    }
    PagingPort port;
    auto client = std::make_unique<geocaching::QueryClient>(port);
    geocaching::Destination address;
    std::array<std::uint8_t, 64> key{};
    std::vector<std::uint8_t> app{0x95, 1, 0xc4, 16};
    app.insert(app.end(), 16, 0);
    app.insert(app.end(), {0xc4, 16});
    app.insert(app.end(), 16, 0);
    app.insert(app.end(), {0, 0xa0});
    assert(client->observe(address, address, {key.data(), 64}, {app.data(), app.size()}, 0));
    assert(client->query({300000000, 1200000000, 310000000, 1210000000}) && client->tick(0));
    assert(client->accept(address, {fixture[0].data(), fixture[0].size()}) && client->tick(1));
    assert(client->accept(address, {fixture[1].data(), fixture[1].size()}) && client->hasMore());
    std::fill(fixture[1].begin(), fixture[1].end(), 0);
    assert(client->loadMore());
    assert(port.sent.size() >= 18);
    assert(port.sent[port.sent.size() - 18] == 0xc4 && port.sent[port.sent.size() - 17] == 16);
    for (std::size_t i = port.sent.size() - 16; i < port.sent.size(); ++i) assert(port.sent[i] == 0x72);
    auto wrong = fixture[2];
    // Snapshot starts after response header, body array header and bin16 header.
    wrong[27] ^= 1;
    assert(!client->accept(address, {wrong.data(), wrong.size()}));
    assert(port.commits == 1);
    port.fail_commit = true;
    assert(!client->accept(address, {fixture[2].data(), fixture[2].size()}));
    assert(client->phase() == geocaching::QueryClientPhase::Querying);
    port.fail_commit = false;
    assert(client->accept(address, {fixture[2].data(), fixture[2].size()}));
    assert(port.commits == 2 && !client->hasMore() && !client->loadMore());
}

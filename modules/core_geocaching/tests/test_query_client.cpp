#include "geocaching/usecase/query_client.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>
struct Port : geocaching::QueryClientPort
{
    unsigned requests = 0, pages = 0;
    bool newRequestId(geocaching::RequestId& id) override
    {
        id.bytes.fill(requests == 0 ? 1 : 3);
        return true;
    }
    geocaching::QueryPersistence submit(const geocaching::DirectoryEntry&, const geocaching::RequestId&, geocaching::ByteView) override
    {
        ++requests;
        return geocaching::QueryPersistence::Committed;
    }
    geocaching::QueryPersistence pollPersistence() override { return geocaching::QueryPersistence::Committed; }
    geocaching::QueryPersistence cancel(const geocaching::Destination&, const geocaching::RequestId&) override { return geocaching::QueryPersistence::Committed; }
    geocaching::QueryPersistence commitCapabilities(const geocaching::Destination&, const geocaching::RequestId&, geocaching::ByteView) override { return geocaching::QueryPersistence::Committed; }
    geocaching::QueryPersistence commitPage(const geocaching::Destination&, const geocaching::RequestId&, geocaching::ByteView,
                                            const geocaching::protocol::QueryPageView& page) override
    {
        geocaching::protocol::CmpReader rows(page.encoded_items);
        geocaching::protocol::SummaryView item;
        assert(page.count == 1 && geocaching::protocol::decodeSummary(rows, item) && item.name == "Test");
        ++pages;
        return geocaching::QueryPersistence::Committed;
    }
};
int main(int argc, char** argv)
{
    assert(argc == 3);
    Port port;
    auto client = std::make_unique<geocaching::QueryClient>(port);
    geocaching::Destination discovery, delivery;
    std::array<std::uint8_t, 64> key{};
    std::vector<std::uint8_t> app{0x95, 1, 0xc4, 16};
    app.insert(app.end(), 16, 0);
    app.insert(app.end(), {0xc4, 16});
    app.insert(app.end(), 16, 0);
    app.insert(app.end(), {0, 0xa1, 'x'});
    assert(client->observe(discovery, delivery, {key.data(), 64}, {app.data(), app.size()}, 0));
    assert(client->query({300000000, 1200000000, 310000000, 1210000000}));
    geocaching::Destination pending_destination;
    geocaching::RequestId pending_id;
    assert(!client->pendingRequest(pending_destination, pending_id));
    assert(client->tick(0) && port.requests == 1);
    assert(client->pendingRequest(pending_destination, pending_id) && pending_destination.bytes == delivery.bytes);
    assert(pending_id.bytes[0] == 1);
    geocaching::RequestId active;
    active.bytes.fill(1);
    assert(client->expectsResponse(delivery, active));
    auto other = delivery;
    other.bytes[0] = 9;
    assert(!client->expectsResponse(other, active));
    active.bytes.fill(2);
    assert(!client->expectsResponse(delivery, active));
    for (int i = 1; i < 3; ++i)
    {
        std::ifstream f(argv[i], std::ios::binary);
        assert(f.good());
        std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(f)), {});
        assert(client->accept(delivery, {data.data(), data.size()}));
        if (i == 1) assert(client->tick(1));
    }
    assert(port.requests == 2 && port.pages == 1 && client->phase() == geocaching::QueryClientPhase::PageReady);
    active.bytes.fill(3);
    assert(!client->expectsResponse(delivery, active));
    assert(!client->pendingRequest(pending_destination, pending_id));
}

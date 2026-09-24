#include "geocaching/usecase/query_client.h"
#include <vector>

struct Port final : geocaching::QueryClientPort
{
    using Result = geocaching::QueryPersistence;
    unsigned requests = 0, stops = 0;
    Result submitted = Result::Committed, polled = Result::Pending;
    bool newRequestId(geocaching::RequestId& id) override
    {
        id.bytes.fill(static_cast<uint8_t>(requests + 1));
        return true;
    }
    Result submit(const geocaching::DirectoryEntry&, const geocaching::RequestId&, geocaching::ByteView) override
    {
        ++requests;
        return submitted;
    }
    Result commitCapabilities(const geocaching::Destination&, const geocaching::RequestId&, geocaching::ByteView) override { return Result::Committed; }
    Result commitPage(const geocaching::Destination&, const geocaching::RequestId&, geocaching::ByteView,
                      const geocaching::protocol::QueryPageView&) override { return Result::Committed; }
    Result pollPersistence() override { return polled; }
    Result cancel(const geocaching::Destination&, const geocaching::RequestId&) override
    {
        ++stops;
        return Result::Pending;
    }
};

int main()
{
    using namespace geocaching;
    Port port;
    QueryClient client(port);
    const protocol::QueryRegion world{-900000000, -1800000000, 900000000, 1800000000};
    const auto timeout = QueryClient::kReplyTimeoutMs;
    if (!client.query(world) || client.tick(0) || client.tick(timeout - 1) || !client.tick(timeout) ||
        client.phase() != QueryClientPhase::Failed || client.failure() != QueryFailure::Timeout || port.stops) return 1;
    std::array<uint8_t, 64> key{};
    std::vector<uint8_t> announce{0x95, 1, 0xc4, 16};
    announce.insert(announce.end(), 16, 0);
    announce.insert(announce.end(), {0xc4, 16});
    announce.insert(announce.end(), 16, 0);
    announce.insert(announce.end(), {0, 0xa1, 'x'});
    if (!client.observe({}, {}, {key.data(), key.size()}, {announce.data(), announce.size()}, 0) ||
        !client.query(world) || !client.tick(0) || client.phase() != QueryClientPhase::CheckingCapabilities) return 2;
    client.tick(0);
    if (client.tick(timeout - 1) || !client.tick(timeout) || client.phase() != QueryClientPhase::Cancelling ||
        client.query(world) || port.stops != 1) return 3;
    if (client.tick(timeout + 1) || !client.persistencePending() || client.accept({}, {key.data(), key.size()})) return 4;
    port.polled = QueryPersistence::Committed;
    if (!client.tick(timeout + 2) || client.persistencePending() || client.phase() != QueryClientPhase::Failed ||
        client.failure() != QueryFailure::Timeout) return 5;
    if (!client.query(world) || client.tick(timeout + 3) || !client.tick(timeout + 5000)) return 6;
    if (!client.cancel()) return 7;
    port.polled = QueryPersistence::Rejected;
    if (client.tick(timeout + 5001) || client.phase() != QueryClientPhase::Failed || client.failure() != QueryFailure::Storage) return 8;
    port.submitted = QueryPersistence::Pending;
    if (!client.query(world) || !client.tick(timeout + 6000) || !client.persistencePending() || client.cancel()) return 9;
    port.polled = QueryPersistence::Committed;
    if (!client.tick(timeout + 6001) || client.persistencePending() || !client.cancel() || !client.tick(timeout + 6002) ||
        client.failure() != QueryFailure::Cancelled) return 10;
    return 0;
}

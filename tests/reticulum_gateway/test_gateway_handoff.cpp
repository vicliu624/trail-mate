#include <cassert>
#include <cstdint>
#include <vector>

static uint32_t now = 200000;
uint32_t millis() { return now; }
namespace reticulum::interfaces
{
constexpr uint8_t kInvalidInterfaceId = 0;
}
enum class LinkState
{
    Pending,
    Active,
    Closed
};
enum class LinkCloseReason
{
    Error
};
struct Resource
{
    bool complete = false;
    bool waiting_for_proof = false;
};
struct LinkSession
{
    LinkState state = LinkState::Active;
    uint8_t interface_id = 32;
    std::vector<int> pending_requests, deferred_payloads, incoming_resource_assemblies;
    std::vector<Resource> incoming_resources, outgoing_resources;
};
struct Interfaces
{
    uint8_t active = 32;
    bool permitted = false;
    uint8_t activeTcpInterfaceId() const { return active; }
    void maintain(bool permit)
    {
        permitted = permit;
        if (permit) active = 35;
    }
};
struct LinkManager
{
    std::vector<LinkSession> sessions;
    template <class Fn>
    void forEachSession(Fn fn)
    {
        for (auto& s : sessions) fn(s);
    }
};
struct PathManager
{
    std::vector<uint8_t> retired;
    void retireInterface(uint8_t id) { retired.push_back(id); }
};
struct Ledger
{
    size_t count = 0;
    size_t size() const { return count; }
};
struct Pages
{
    bool pending = false;
    bool empty() const { return !pending; }
};
struct Propagation
{
    bool uploads = false;
    size_t deliveries = 0;
    bool hasPendingUploads() const { return uploads; }
    size_t pendingDeliveryCount() const { return deliveries; }
};
struct Deferred
{
    unsigned clears = 0;
    void clear() { ++clears; }
};
class LxmfAdapter
{
  public:
    Interfaces interfaces_;
    LinkManager link_manager_;
    PathManager path_manager_;
    Ledger delivery_attempt_ledger_;
    Pages network_page_client_;
    Propagation propagation_client_;
    Deferred deferred_discovery_;
    uint32_t gateway_custom_activity_ms_ = 0;
    bool gateway_custom_activity_seen_ = false;
    unsigned closed = 0;
    void closeLinkSession(LinkSession& session, LinkCloseReason)
    {
        session.state = LinkState::Closed;
        ++closed;
    }
    void maintainGatewayDiscovery();
};
#include "gateway_handoff_actual.inc"

int main()
{
    for (unsigned condition = 0; condition < 11; ++condition)
    {
        LxmfAdapter adapter;
        adapter.link_manager_.sessions.emplace_back();
        auto& session = adapter.link_manager_.sessions.front();
        switch (condition)
        {
        case 0:
            session.state = LinkState::Pending;
            break;
        case 1:
            session.pending_requests.push_back(1);
            break;
        case 2:
            session.deferred_payloads.push_back(1);
            break;
        case 3:
            session.incoming_resource_assemblies.push_back(1);
            break;
        case 4:
            session.incoming_resources.push_back({false, false});
            break;
        case 5:
            session.outgoing_resources.push_back({false, false});
            break;
        case 6:
            session.outgoing_resources.push_back({true, true});
            break;
        case 7:
            adapter.delivery_attempt_ledger_.count = 1;
            break;
        case 8:
            adapter.network_page_client_.pending = true;
            break;
        case 9:
            adapter.propagation_client_.uploads = true;
            break;
        case 10:
            adapter.propagation_client_.deliveries = 1;
            break;
        }
        adapter.maintainGatewayDiscovery();
        assert(!adapter.interfaces_.permitted && adapter.interfaces_.active == 32);
        assert(adapter.path_manager_.retired.empty() && adapter.closed == 0);
    }
    LxmfAdapter idle;
    idle.link_manager_.sessions.emplace_back();
    idle.link_manager_.sessions.front().incoming_resources.push_back({true, false});
    idle.link_manager_.sessions.front().outgoing_resources.push_back({true, false});
    idle.link_manager_.sessions.emplace_back();
    idle.link_manager_.sessions.back().interface_id = 35;
    idle.maintainGatewayDiscovery();
    assert(idle.interfaces_.permitted && idle.interfaces_.active == 35);
    assert(idle.closed == 1 && idle.link_manager_.sessions.front().state == LinkState::Closed);
    assert(idle.link_manager_.sessions.back().state == LinkState::Active);
    assert(idle.path_manager_.retired == std::vector<uint8_t>{32} && idle.deferred_discovery_.clears == 1);
    idle.maintainGatewayDiscovery();
    assert(idle.closed == 1 && idle.path_manager_.retired.size() == 1);
    LxmfAdapter custom;
    custom.gateway_custom_activity_seen_ = true;
    custom.gateway_custom_activity_ms_ = UINT32_MAX - 1000;
    now = custom.gateway_custom_activity_ms_ + 119999;
    custom.maintainGatewayDiscovery();
    assert(!custom.interfaces_.permitted);
    ++now;
    custom.maintainGatewayDiscovery();
    assert(custom.interfaces_.permitted);
}

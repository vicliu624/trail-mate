#include "chat/infra/mesh_adapter_router_core.h"
#include <cassert>

class Backend final : public chat::IMeshAdapter
{
  public:
    unsigned sends = 0;
    bool sendText(chat::ChannelId, const std::string&, chat::MessageId*, chat::NodeId) override
    {
        ++sends;
        return true;
    }
    bool pollIncomingText(chat::MeshIncomingText*) override { return false; }
    bool sendAppData(chat::ChannelId, std::uint32_t, const std::uint8_t*, std::size_t,
                     chat::NodeId, bool, chat::MessageId, bool) override { return false; }
    bool pollIncomingData(chat::MeshIncomingData*) override { return false; }
    void applyConfig(const chat::MeshConfig&) override {}
    bool isReady() const override { return true; }
    bool pollIncomingRawPacket(std::uint8_t*, std::size_t&, std::size_t) override { return false; }
};

int main()
{
    chat::MeshAdapterRouterCore router;
    auto active = std::make_unique<Backend>();
    auto* active_ptr = active.get();
    assert(router.installBackend(chat::MeshProtocol::MeshCore, std::move(active)));
    auto service = std::make_unique<Backend>();
    auto* service_ptr = service.get();
    assert(router.installServiceBackend(chat::MeshProtocol::Reticulum, std::move(service)));
    assert(router.backendProtocol() == chat::MeshProtocol::MeshCore);
    assert(!router.takeServiceBackend(chat::MeshProtocol::Reticulum, active_ptr));
    assert(router.backendForProtocol(chat::MeshProtocol::Reticulum) == service_ptr);
    router.setActiveProtocol(chat::MeshProtocol::RNode);
    assert(!router.takeServiceBackend(chat::MeshProtocol::Reticulum, service_ptr));
    router.setActiveProtocol(chat::MeshProtocol::MeshCore);
    auto detached = router.takeServiceBackend(chat::MeshProtocol::Reticulum, service_ptr);
    assert(detached.get() == service_ptr && !router.isServiceBackend(chat::MeshProtocol::Reticulum));
    assert(router.installServiceBackend(chat::MeshProtocol::Reticulum, std::move(detached)));
    assert(router.backendForProtocol(chat::MeshProtocol::Reticulum) == service_ptr);
    assert(router.sendText(chat::ChannelId::PRIMARY, "chat", nullptr, 0));
    assert(active_ptr->sends == 1 && service_ptr->sends == 0);
    assert(!router.installServiceBackend(chat::MeshProtocol::Reticulum, std::make_unique<Backend>()));
    assert(router.backendForProtocol(chat::MeshProtocol::Reticulum) == service_ptr);
    assert(!router.installServiceBackend(chat::MeshProtocol::Meshtastic, nullptr));
    assert(router.backendProtocol() == chat::MeshProtocol::MeshCore);
    // A former active chat backend is not a service, but can be retired after
    // the user switches away. Neither spelling of the active shared slot may go.
    chat::MeshAdapterRouterCore switched;
    auto old_reticulum = std::make_unique<Backend>();
    auto* old_ptr = old_reticulum.get();
    assert(switched.installBackend(chat::MeshProtocol::Reticulum, std::move(old_reticulum)));
    assert(!switched.takeInactiveBackend(chat::MeshProtocol::Reticulum, old_ptr));
    assert(!switched.takeInactiveBackend(chat::MeshProtocol::RNode, old_ptr));
    auto current = std::make_unique<Backend>();
    auto* current_ptr = current.get();
    assert(switched.installBackend(chat::MeshProtocol::MeshCore, std::move(current)));
    assert(!switched.takeServiceBackend(chat::MeshProtocol::Reticulum));
    assert(!switched.takeInactiveBackend(chat::MeshProtocol::Reticulum, current_ptr));
    auto retired = switched.takeInactiveBackend(chat::MeshProtocol::Reticulum, old_ptr);
    assert(retired.get() == old_ptr && switched.backendProtocol() == chat::MeshProtocol::MeshCore);
    assert(switched.sendText(chat::ChannelId::PRIMARY, "chat after retirement", nullptr, 0));
    assert(current_ptr->sends == 1);
    assert(switched.installServiceBackend(chat::MeshProtocol::Reticulum, std::make_unique<Backend>()));
    assert(switched.backendProtocol() == chat::MeshProtocol::MeshCore);
}

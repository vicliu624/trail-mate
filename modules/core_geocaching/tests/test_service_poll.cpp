#include "chat/infra/mesh_adapter_router_core.h"
#include <cassert>

class PollBackend final : public chat::IMeshAdapter
{
  public:
    unsigned polls = 0;
    bool sendText(chat::ChannelId, const std::string&, chat::MessageId*, chat::NodeId) override { return false; }
    bool pollIncomingText(chat::MeshIncomingText*) override { return false; }
    bool sendAppData(chat::ChannelId, std::uint32_t, const std::uint8_t*, std::size_t,
                     chat::NodeId, bool, chat::MessageId, bool) override { return false; }
    bool pollIncomingData(chat::MeshIncomingData*) override { return false; }
    void applyConfig(const chat::MeshConfig&) override {}
    bool isReady() const override { return false; }
    bool pollIncomingRawPacket(std::uint8_t*, std::size_t&, std::size_t) override { return false; }
    void processSendQueue() override { ++polls; }
};
int main()
{
    chat::MeshAdapterRouterCore router;
    auto active = std::make_unique<PollBackend>();
    auto* a = active.get();
    assert(router.installBackend(chat::MeshProtocol::Meshtastic, std::move(active)));
    auto service = std::make_unique<PollBackend>();
    auto* s = service.get();
    assert(router.installServiceBackend(chat::MeshProtocol::Reticulum, std::move(service)));
    assert(router.processServiceQueue(chat::MeshProtocol::Reticulum));
    assert(s->polls == 1 && a->polls == 0);
    assert(!router.processServiceQueue(chat::MeshProtocol::Meshtastic));
    router.processSendQueue();
    assert(a->polls == 1 && s->polls == 1);
    router.setActiveProtocol(chat::MeshProtocol::RNode);
    assert(!router.processServiceQueue(chat::MeshProtocol::Reticulum));
    assert(!router.processServiceQueue(chat::MeshProtocol::RNode));
    assert(!router.processServiceQueue(static_cast<chat::MeshProtocol>(255)));
    router.processSendQueue();
    assert(s->polls == 2);
}

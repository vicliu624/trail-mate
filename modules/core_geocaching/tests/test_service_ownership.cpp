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
    auto radio = std::make_unique<PollBackend>();
    auto* previous = radio.get();
    assert(router.installBackend(chat::MeshProtocol::Reticulum, std::move(radio)));
    assert(router.installBackend(chat::MeshProtocol::MeshCore, std::make_unique<PollBackend>()));
    assert(!router.processServiceQueue(chat::MeshProtocol::Reticulum));
    assert(!router.processServiceQueue(chat::MeshProtocol::RNode));
    assert(previous->polls == 0);

    chat::MeshAdapterRouterCore replaced;
    assert(replaced.installServiceBackend(chat::MeshProtocol::RNode, std::make_unique<PollBackend>()));
    assert(replaced.processServiceQueue(chat::MeshProtocol::Reticulum));
    auto replacement = std::make_unique<PollBackend>();
    auto* replacement_ptr = replacement.get();
    assert(replaced.installBackend(chat::MeshProtocol::Reticulum, std::move(replacement)));
    replaced.setActiveProtocol(chat::MeshProtocol::Meshtastic);
    assert(!replaced.processServiceQueue(chat::MeshProtocol::RNode));
    assert(replacement_ptr->polls == 0);
    return 0;
}

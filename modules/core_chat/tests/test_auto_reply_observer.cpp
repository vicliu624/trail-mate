#include "chat/domain/chat_model.h"
#include "chat/infra/store/ram_store.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/usecase/auto_reply_observer.h"
#include <cassert>
#include <cstring>

namespace
{
bool enabled = true;
bool sleeping = false;
std::string reply = "asleep";
bool readReply(std::string& out)
{
    out = reply;
    return enabled;
}
bool isSleeping() { return sleeping; }
chat::NodeId selfNode() { return 123; }

class Adapter : public chat::IMeshAdapter
{
  public:
    bool sendText(chat::ChannelId, const std::string& text, chat::MessageId* id, chat::NodeId peer) override
    {
        ++sent;
        last_peer = peer;
        last_text = text;
        if (id) *id = sent;
        return true;
    }
    chat::MeshSendResult sendTextToReticulumDestination(chat::ChannelId,
                                                        const std::string& text, chat::MessageId, const chat::ReticulumPeerIdentity& destination) override
    {
        last_destination = destination;
        last_text = text;
        return chat::MeshSendResult::success(++sent);
    }
    bool pollIncomingText(chat::MeshIncomingText* out) override
    {
        if (!pending) return false;
        *out = incoming;
        pending = false;
        return true;
    }
    bool sendAppData(chat::ChannelId, uint32_t, const uint8_t*, size_t, chat::NodeId,
                     bool, chat::MessageId, bool) override { return false; }
    bool pollIncomingData(chat::MeshIncomingData*) override { return false; }
    void applyConfig(const chat::MeshConfig&) override {}
    bool isReady() const override { return true; }
    chat::NodeId getNodeId() const override { return selfNode(); }
    bool pollIncomingRawPacket(uint8_t*, size_t&, size_t) override { return false; }
    int sent = 0;
    chat::NodeId last_peer = 0;
    std::string last_text;
    chat::ReticulumPeerIdentity last_destination{};
    chat::MeshIncomingText incoming{};
    bool pending = false;
};
} // namespace

int main()
{
    for (auto protocol : {chat::MeshProtocol::Meshtastic, chat::MeshProtocol::MeshCore,
                          chat::MeshProtocol::Reticulum})
    {
        chat::ChatModel model;
        chat::RamStore store;
        Adapter adapter;
        chat::ChatService service(model, adapter, store);
        service.setActiveProtocol(protocol);
        chat::auto_reply::Observer observer(service, {readReply, isSleeping, selfNode, 171});
        adapter.incoming.from = 456;
        adapter.incoming.to = selfNode();
        adapter.incoming.text = "hello";
        if (protocol == chat::MeshProtocol::Reticulum)
        {
            uint8_t destination[16], identity[16];
            std::memset(destination, 0x42, sizeof(destination));
            std::memset(identity, 0x24, sizeof(identity));
            adapter.incoming.reticulum_identity = chat::makeReticulumPeerIdentity(destination, identity);
        }
        auto receive = [&](uint32_t id)
        {
            adapter.incoming.msg_id = id;
            adapter.pending = true;
            service.processIncoming();
        };
        sleeping = false;
        receive(10);
        assert(adapter.sent == 0);
        sleeping = true;
        receive(11);
        assert(adapter.sent == 1 && adapter.last_text == reply);
        if (protocol == chat::MeshProtocol::Reticulum)
            assert(std::memcmp(adapter.last_destination.destination_hash,
                               adapter.incoming.reticulum_identity.destination_hash, 16) == 0);
        else assert(adapter.last_peer == 456);
        receive(11); // A repeated radio frame must not generate another reply.
        assert(adapter.sent == 1);
        enabled = false;
        receive(12);
        enabled = true;
        reply = "   ";
        receive(13);
        reply.assign(172, 'x');
        receive(14);
        reply = "asleep";
        adapter.incoming.to = 0xFFFFFFFFU;
        receive(15);
        assert(adapter.sent == 1);
    }
}

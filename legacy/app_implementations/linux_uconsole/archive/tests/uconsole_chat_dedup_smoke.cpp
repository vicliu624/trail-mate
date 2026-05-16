#include "chat/domain/chat_model.h"
#include "chat/infra/store/ram_store.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/usecase/chat_service.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <string>
#include <vector>

namespace
{

class FakeMeshAdapter final : public ::chat::IMeshAdapter
{
  public:
    void pushIncoming(::chat::NodeId from,
                      ::chat::MessageId msg_id,
                      const std::string& text)
    {
        ::chat::MeshIncomingText incoming{};
        incoming.channel = ::chat::ChannelId::PRIMARY;
        incoming.from = from;
        incoming.to = 0xFFFFFFFFUL;
        incoming.msg_id = msg_id;
        incoming.text = text;
        incoming.timestamp = 1;
        incoming.hop_limit = 3;
        incoming.encrypted = true;
        incoming_.push_back(incoming);
    }

    bool sendText(::chat::ChannelId,
                  const std::string&,
                  ::chat::MessageId* out_msg_id,
                  ::chat::NodeId = 0) override
    {
        if (out_msg_id != nullptr)
        {
            *out_msg_id = 0;
        }
        return false;
    }

    bool pollIncomingText(::chat::MeshIncomingText* out) override
    {
        if (incoming_.empty())
        {
            return false;
        }
        if (out != nullptr)
        {
            *out = incoming_.front();
        }
        incoming_.pop_front();
        return true;
    }

    bool sendAppData(::chat::ChannelId,
                     std::uint32_t,
                     const std::uint8_t*,
                     std::size_t,
                     ::chat::NodeId = 0,
                     bool = false,
                     ::chat::MessageId = 0,
                     bool = false) override
    {
        return false;
    }

    bool pollIncomingData(::chat::MeshIncomingData*) override
    {
        return false;
    }

    void applyConfig(const ::chat::MeshConfig&) override {}

    bool isReady() const override
    {
        return true;
    }

    bool pollIncomingRawPacket(std::uint8_t*, std::size_t&, std::size_t) override
    {
        return false;
    }

  private:
    std::deque<::chat::MeshIncomingText> incoming_{};
};

} // namespace

int expect(bool condition, const char* message)
{
    if (condition)
    {
        return 0;
    }
    std::cerr << message << '\n';
    return 1;
}

int main()
{
    ::chat::ChatModel model;
    FakeMeshAdapter adapter;
    ::chat::RamStore store;
    ::chat::ChatService service(model,
                                adapter,
                                store,
                                ::chat::MeshProtocol::Meshtastic);
    const ::chat::ConversationId broadcast(::chat::ChannelId::PRIMARY,
                                           0,
                                           ::chat::MeshProtocol::Meshtastic);

    adapter.pushIncoming(0x1234ABCDU, 0x42U, "test");
    adapter.pushIncoming(0x1234ABCDU, 0x42U, "test");
    adapter.pushIncoming(0x1234ABCDU, 0x42U, "test");
    service.processIncoming();

    auto messages = store.loadRecent(broadcast, 10);
    if (int rc = expect(messages.size() == 1U,
                        "duplicate incoming text was stored more than once"))
    {
        return rc;
    }
    if (int rc = expect(messages.front().from == 0x1234ABCDU,
                        "stored message sender changed"))
    {
        return rc;
    }
    if (int rc = expect(messages.front().msg_id == 0x42U,
                        "stored message id changed"))
    {
        return rc;
    }
    if (int rc = expect(messages.front().text == "test",
                        "stored message text changed"))
    {
        return rc;
    }
    if (int rc = expect(store.getUnread(broadcast) == 1,
                        "duplicate incoming text inflated unread count"))
    {
        return rc;
    }

    adapter.pushIncoming(0x1234ABCDU, 0x43U, "next");
    adapter.pushIncoming(0x0000BEEFU, 0x42U, "same id from another node");
    service.processIncoming();

    messages = store.loadRecent(broadcast, 10);
    if (int rc = expect(messages.size() == 3U,
                        "distinct incoming identities were incorrectly suppressed"))
    {
        return rc;
    }
    if (int rc = expect(messages[1].msg_id == 0x43U,
                        "new message id from same node was suppressed"))
    {
        return rc;
    }
    if (int rc = expect(messages[2].from == 0x0000BEEFU,
                        "same message id from another node was suppressed"))
    {
        return rc;
    }
    if (int rc = expect(messages[2].msg_id == 0x42U,
                        "message id from another node changed"))
    {
        return rc;
    }
    if (int rc = expect(store.getUnread(broadcast) == 3,
                        "unread count does not match unique incoming messages"))
    {
        return rc;
    }

    return 0;
}

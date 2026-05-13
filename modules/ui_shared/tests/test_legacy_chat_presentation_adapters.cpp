#include "chat/domain/chat_model.h"
#include "chat/infra/store/ram_store.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/usecase/chat_service.h"
#include "ui/presentation_sources/legacy_chat_action_sink.h"
#include "ui/presentation_sources/legacy_chat_presentation_source.h"

#include <cassert>
#include <cstring>
#include <string>

namespace
{

class FakeMeshAdapter final : public ::chat::IMeshAdapter
{
  public:
    bool sendText(::chat::ChannelId channel,
                  const std::string& text,
                  ::chat::MessageId* out_msg_id,
                  ::chat::NodeId peer = 0) override
    {
        ++send_count;
        last_channel = channel;
        last_text = text;
        last_peer = peer;
        if (out_msg_id)
        {
            *out_msg_id = next_id++;
        }
        return send_ok;
    }

    bool pollIncomingText(::chat::MeshIncomingText*) override { return false; }
    bool sendAppData(::chat::ChannelId,
                     uint32_t,
                     const uint8_t*,
                     size_t,
                     ::chat::NodeId = 0,
                     bool = false,
                     ::chat::MessageId = 0,
                     bool = false) override
    {
        return false;
    }
    bool pollIncomingData(::chat::MeshIncomingData*) override { return false; }
    void applyConfig(const ::chat::MeshConfig&) override {}
    bool isReady() const override { return true; }
    bool pollIncomingRawPacket(uint8_t*, size_t&, size_t) override { return false; }

    int send_count = 0;
    bool send_ok = true;
    ::chat::MessageId next_id = 100;
    ::chat::ChannelId last_channel = ::chat::ChannelId::PRIMARY;
    std::string last_text;
    ::chat::NodeId last_peer = 0;
};

ui::chat::ConversationId directPeer(uint32_t peer)
{
    ui::chat::ConversationId id;
    id.kind = ui::chat::ConversationKind::DirectPeer;
    id.protocol = ui::chat::ChatProtocolKind::Meshtastic;
    id.primary = peer;
    id.secondary = static_cast<uint32_t>(::chat::ChannelId::PRIMARY);
    return id;
}

ui::chat::ConversationId teamConversation()
{
    ui::chat::ConversationId id;
    id.kind = ui::chat::ConversationKind::Team;
    id.protocol = ui::chat::ChatProtocolKind::TrailMate;
    id.primary = 7;
    return id;
}

} // namespace

int main()
{
    ::chat::ChatModel model;
    FakeMeshAdapter mesh;
    ::chat::RamStore store;
    ::chat::ChatService service(model, mesh, store);

    ui::presentation_sources::LegacyChatActionSink sink(service);
    ui::presentation_sources::LegacyChatPresentationSource source(service);

    const ui::chat::ConversationId ada = directPeer(1234);
    ui::chat::SendMessageView send;
    send.conversation = ada;
    send.text = "hello";
    send.text_len = 5;

    const auto send_result = sink.sendMessage(send);
    assert(send_result.ok);
    assert(mesh.send_count == 1);
    assert(mesh.last_peer == 1234);
    assert(mesh.last_text == "hello");

    ui::chat::ChatWorkspaceRequest request;
    request.selected = ada;
    ui::chat::ChatWorkspaceSnapshot snapshot;
    assert(source.buildChatWorkspaceSnapshot(request, snapshot));
    assert(snapshot.header.valid);
    assert(snapshot.conversation_count == 1);
    assert(snapshot.conversations[0].id == ada);
    assert(snapshot.conversations[0].selected);
    assert(std::strcmp(snapshot.conversations[0].title.c_str(), "04D2") == 0);
    assert(snapshot.message_count == 1);
    assert(snapshot.messages[0].conversation == ada);
    assert(snapshot.messages[0].outgoing);
    assert(snapshot.messages[0].delivery == ui::chat::MessageDeliveryState::Queued);
    assert(std::strcmp(snapshot.messages[0].text.c_str(), "hello") == 0);
    assert(snapshot.can_send);
    assert(snapshot.composer_enabled);

    assert(sink.markRead(ada).ok);

    const ui::chat::ConversationId team = teamConversation();
    assert(!sink.selectConversation(team).ok);
    assert(!sink.markRead(team).ok);
    send.conversation = team;
    const auto team_send = sink.sendMessage(send);
    assert(!team_send.ok);
    assert(team_send.failure == ui::UiActionFailure::Unsupported);

    return 0;
}

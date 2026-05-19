#include "chat/delivery/chat_delivery_read_model.h"
#include "chat/delivery/legacy_chat_delivery_bridge.h"
#include "chat/domain/chat_model.h"
#include "chat/infra/store/ram_store.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/usecase/chat_service.h"
#include "ui/presentation_sources/chat_presentation_source.h"
#include "ui/presentation_sources/legacy_chat_action_sink.h"

#include <cassert>
#include <cstring>
#include <deque>
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

    bool pollIncomingText(::chat::MeshIncomingText* out) override
    {
        if (!out || incoming.empty())
        {
            return false;
        }
        *out = incoming.front();
        incoming.pop_front();
        return true;
    }
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
    std::deque<::chat::MeshIncomingText> incoming;
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

ui::chat::ConversationId broadcastConversation()
{
    ui::chat::ConversationId id;
    id.kind = ui::chat::ConversationKind::Channel;
    id.protocol = ui::chat::ChatProtocolKind::Meshtastic;
    id.primary = static_cast<uint32_t>(::chat::ChannelId::PRIMARY);
    return id;
}

ui::chat::ConversationId systemConversation()
{
    ui::chat::ConversationId id;
    id.kind = ui::chat::ConversationKind::System;
    id.protocol = ui::chat::ChatProtocolKind::TrailMate;
    id.primary = 1;
    return id;
}

} // namespace

int main()
{
    ::chat::ChatModel model;
    FakeMeshAdapter mesh;
    ::chat::RamStore store;
    ::chat::ChatService service(model, mesh, store);
    ::chat::delivery::ChatDeliveryReadModel delivery_read_model;

    ui::presentation_sources::LegacyChatActionSink sink(service);
    ui::presentation_sources::ChatPresentationSource source(
        service, nullptr, &delivery_read_model);

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

    ::chat::delivery::ChatDeliveryRecord delivered{};
    delivered.ref.protocol_id = 100;
    delivered.state = ::chat::delivery::DeliveryState::Delivered;
    delivered.failure = ::chat::delivery::DeliveryFailureKind::None;
    assert(delivery_read_model.upsert(delivered));

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
    assert(snapshot.messages[0].delivery ==
           ui::chat::MessageDeliveryState::Delivered);
    assert(snapshot.messages[0].failure == ui::chat::MessageFailureKind::None);
    assert(std::strcmp(snapshot.messages[0].text.c_str(), "hello") == 0);
    assert(snapshot.can_send);
    assert(snapshot.composer_enabled);

    mesh.send_ok = false;
    const ui::chat::SendMessageView failed_send{ada, "fail", 4};
    const auto rejected_send = sink.sendMessage(failed_send);
    assert(!rejected_send.ok);
    assert(rejected_send.failure == ui::UiActionFailure::Rejected);
    assert(delivery_read_model.upsert(::chat::delivery::toFailedDeliveryRecord(
        ::chat::delivery::ChatDeliveryRef{0, 101, 0},
        ::chat::delivery::SendFailureKind::PeerKeyMissing)));
    assert(source.buildChatWorkspaceSnapshot(request, snapshot));
    assert(snapshot.message_count == 2);
    assert(snapshot.messages[1].delivery == ui::chat::MessageDeliveryState::Failed);
    assert(snapshot.messages[1].failure ==
           ui::chat::MessageFailureKind::PeerKeyMissing);

    assert(sink.markRead(ada).ok);

    ::chat::MeshIncomingText incoming{};
    incoming.channel = ::chat::ChannelId::PRIMARY;
    incoming.from = 0x648144D4;
    incoming.to = 0xFFFFFFFFUL;
    incoming.msg_id = 900;
    incoming.text = "broadcast hello";
    mesh.incoming.push_back(incoming);
    service.processIncoming();

    const ui::chat::ConversationId broadcast = broadcastConversation();
    request.selected = broadcast;
    assert(source.buildChatWorkspaceSnapshot(request, snapshot));
    assert(snapshot.header.valid);
    assert(snapshot.message_count == 1);
    assert(snapshot.messages[0].conversation == broadcast);
    assert(!snapshot.messages[0].outgoing);
    assert(snapshot.messages[0].sender_node_id == 0x648144D4);
    assert(std::strcmp(snapshot.messages[0].sender_label.c_str(), "44D4") == 0);

    const ui::chat::ConversationId team = teamConversation();
    assert(!sink.selectConversation(team).ok);
    assert(!sink.markRead(team).ok);
    send.conversation = team;
    const auto team_send = sink.sendMessage(send);
    assert(!team_send.ok);
    assert(team_send.failure == ui::UiActionFailure::Unsupported);

    request.selected = team;
    assert(source.buildChatWorkspaceSnapshot(request, snapshot));
    assert(!snapshot.can_send);
    assert(!snapshot.composer_enabled);

    send.conversation = broadcast;
    mesh.send_ok = true;
    const auto broadcast_send = sink.sendMessage(send);
    assert(broadcast_send.ok);

    const ui::chat::ConversationId system = systemConversation();
    send.conversation = system;
    const auto system_send = sink.sendMessage(send);
    assert(!system_send.ok);
    assert(system_send.failure == ui::UiActionFailure::Unsupported);

    return 0;
}

#include "chat/delivery/chat_message_ledger.h"
#include "chat/domain/conversation_name.h"
#include "chat/infra/store/ram_store.h"

#include <cassert>

namespace
{

class RecordingDeliveryEventPort final
    : public chat::delivery::IChatDeliveryEventPort
{
  public:
    void publishDeliveryEvent(
        const chat::delivery::ChatDeliveryEvent& event) override
    {
        last = event;
        ++count;
    }

    chat::delivery::ChatDeliveryEvent last{};
    int count = 0;
};

class TransientStore final : public chat::RamStore
{
  public:
    bool appendDurably(const chat::ChatMessage& message) override
    {
        if (append_failures > 0)
        {
            --append_failures;
            return false;
        }
        append(message);
        return true;
    }

    bool updateMessageStatusForProtocol(chat::MessageId msg_id,
                                        chat::MeshProtocol protocol,
                                        chat::MessageStatus status) override
    {
        if (status_failures > 0)
        {
            --status_failures;
            return false;
        }
        return RamStore::updateMessageStatusForProtocol(msg_id,
                                                        protocol,
                                                        status);
    }

    bool getMessageForProtocol(chat::MessageId msg_id,
                               chat::MeshProtocol protocol,
                               chat::ChatMessage* out) const override
    {
        ++protocol_lookup_count;
        return RamStore::getMessageForProtocol(msg_id, protocol, out);
    }

    int append_failures = 0;
    int status_failures = 0;
    mutable int protocol_lookup_count = 0;
};

chat::ChatMessage outgoing(chat::MessageId id, chat::MessageStatus status)
{
    chat::ChatMessage message;
    message.protocol = chat::MeshProtocol::Meshtastic;
    message.channel = chat::ChannelId::PRIMARY;
    message.from = 0;
    message.peer = 0xAABBCCDD;
    message.msg_id = id;
    message.text = "ledger";
    message.status = status;
    return message;
}

chat::ChatMessage outgoing_with_protocol(chat::MessageId id,
                                         chat::MeshProtocol protocol,
                                         chat::NodeId peer)
{
    chat::ChatMessage message = outgoing(id, chat::MessageStatus::Queued);
    message.protocol = protocol;
    message.peer = peer;
    return message;
}

chat::ChatMessage incoming(chat::MessageId id)
{
    chat::ChatMessage message = outgoing(id, chat::MessageStatus::Incoming);
    message.from = 0x11223344;
    return message;
}

} // namespace

int main()
{
    chat::ChatModel model;
    chat::RamStore store;
    chat::delivery::ChatMessageLedger ledger(model, store);
    RecordingDeliveryEventPort delivery_events{};
    ledger.setDeliveryEventPort(&delivery_events);

    ledger.recordOutbound(outgoing(100, chat::MessageStatus::Queued), true);
    const chat::ChatMessage* model_message = model.getMessage(100);
    assert(model_message != nullptr);
    assert(model_message->status == chat::MessageStatus::Queued);
    assert(delivery_events.count == 1);
    assert(delivery_events.last.ref.protocol_id == 100);
    assert(delivery_events.last.ref.protocol ==
           static_cast<uint8_t>(chat::MeshProtocol::Meshtastic));
    assert(delivery_events.last.state == chat::delivery::DeliveryState::Queued);

    chat::ChatMessage stored{};
    assert(store.getMessage(100, &stored));
    assert(stored.status == chat::MessageStatus::Queued);

    assert(ledger.applyOutboundStatus(100,
                                      chat::MessageStatus::Sent,
                                      true,
                                      1234));
    assert(model.getMessage(100)->status == chat::MessageStatus::Sent);
    assert(store.getMessage(100, &stored));
    assert(stored.status == chat::MessageStatus::Sent);
    assert(delivery_events.count == 2);
    assert(delivery_events.last.ref.protocol_id == 100);
    assert(delivery_events.last.ref.protocol ==
           static_cast<uint8_t>(chat::MeshProtocol::Meshtastic));
    assert(delivery_events.last.state == chat::delivery::DeliveryState::Sent);
    assert(delivery_events.last.timestamp_ms == 1234);

    assert(!ledger.applyOutboundStatus(100, chat::MessageStatus::Queued, true));
    assert(delivery_events.count == 2);
    assert(model.getMessage(100)->status == chat::MessageStatus::Sent);
    assert(!ledger.applyOutboundStatus(100, chat::MessageStatus::Failed, true));
    assert(delivery_events.count == 2);
    assert(model.getMessage(100)->status == chat::MessageStatus::Sent);

    assert(ledger.applyOutboundStatus(100,
                                      chat::MessageStatus::Delivered,
                                      true));
    assert(model.getMessage(100)->status == chat::MessageStatus::Delivered);
    assert(delivery_events.count == 3);
    assert(delivery_events.last.state ==
           chat::delivery::DeliveryState::Delivered);
    assert(!ledger.applyOutboundStatus(100, chat::MessageStatus::Failed, true));
    assert(delivery_events.count == 3);
    assert(model.getMessage(100)->status == chat::MessageStatus::Delivered);

    ledger.recordOutbound(outgoing(200, chat::MessageStatus::Failed),
                          true,
                          chat::delivery::SendFailureKind::PeerKeyMissing);
    assert(model.getMessage(200)->status == chat::MessageStatus::Failed);
    assert(delivery_events.count == 4);
    assert(delivery_events.last.ref.protocol_id == 200);
    assert(delivery_events.last.ref.protocol ==
           static_cast<uint8_t>(chat::MeshProtocol::Meshtastic));
    assert(delivery_events.last.state == chat::delivery::DeliveryState::Failed);
    assert(delivery_events.last.failure ==
           chat::delivery::SendFailureKind::PeerKeyMissing);
    assert(!ledger.applyOutboundStatus(200, chat::MessageStatus::Queued, true));
    assert(!ledger.applyOutboundStatus(200, chat::MessageStatus::Sent, true));
    assert(delivery_events.count == 4);
    assert(model.getMessage(200)->status == chat::MessageStatus::Failed);
    assert(ledger.markRetryQueued(200, true));
    assert(model.getMessage(200)->status == chat::MessageStatus::Queued);
    assert(delivery_events.count == 5);
    assert(delivery_events.last.ref.protocol_id == 200);
    assert(delivery_events.last.ref.protocol ==
           static_cast<uint8_t>(chat::MeshProtocol::Meshtastic));
    assert(delivery_events.last.state == chat::delivery::DeliveryState::Queued);

    model.onIncoming(incoming(300));
    store.append(incoming(300));
    assert(!ledger.applyOutboundStatus(300, chat::MessageStatus::Sent, true));
    assert(!ledger.markRetryQueued(300, true));
    assert(delivery_events.count == 5);

    chat::ChatModel store_only_model;
    chat::RamStore store_only_store;
    chat::delivery::ChatMessageLedger store_only_ledger(store_only_model,
                                                        store_only_store);
    store_only_ledger.recordOutbound(
        outgoing(400, chat::MessageStatus::Queued),
        false);
    assert(store_only_model.getMessage(400) == nullptr);
    assert(store_only_ledger.applyOutboundStatus(400,
                                                 chat::MessageStatus::Sent,
                                                 false));
    assert(store_only_store.getMessage(400, &stored));
    assert(stored.status == chat::MessageStatus::Sent);

    chat::ChatModel collision_model;
    chat::RamStore collision_store;
    chat::delivery::ChatMessageLedger collision_ledger(collision_model,
                                                       collision_store);
    collision_ledger.recordOutbound(
        outgoing_with_protocol(500,
                               chat::MeshProtocol::Meshtastic,
                               0x01020304),
        true);
    collision_ledger.recordOutbound(
        outgoing_with_protocol(500,
                               chat::MeshProtocol::Reticulum,
                               0x05060708),
        true);
    assert(collision_ledger.applyOutboundStatusForProtocol(
        500,
        chat::MeshProtocol::Reticulum,
        chat::MessageStatus::Delivered,
        true));
    assert(collision_ledger.applyOutboundStatusForProtocol(
        500,
        chat::MeshProtocol::Meshtastic,
        chat::MessageStatus::Failed,
        true,
        2222,
        chat::delivery::SendFailureKind::AckTimeout));
    const chat::ChatMessage* meshtastic_collision =
        collision_model.getMessageForProtocol(500,
                                              chat::MeshProtocol::Meshtastic);
    const chat::ChatMessage* reticulum_collision =
        collision_model.getMessageForProtocol(500,
                                              chat::MeshProtocol::Reticulum);
    assert(meshtastic_collision != nullptr);
    assert(reticulum_collision != nullptr);
    assert(meshtastic_collision->status == chat::MessageStatus::Failed);
    assert(reticulum_collision->status == chat::MessageStatus::Delivered);
    assert(collision_ledger.markRetryQueuedForProtocol(
        500,
        chat::MeshProtocol::Meshtastic,
        true));
    assert(meshtastic_collision->status == chat::MessageStatus::Queued);
    assert(collision_store.getMessageForProtocol(
        500,
        chat::MeshProtocol::Meshtastic,
        &stored));
    assert(stored.status == chat::MessageStatus::Queued);
    assert(collision_store.getMessageForProtocol(
        500,
        chat::MeshProtocol::Reticulum,
        &stored));
    assert(stored.status == chat::MessageStatus::Delivered);

    chat::ChatModel deferred_model;
    TransientStore deferred_store;
    deferred_store.append_failures = 1;
    chat::delivery::ChatMessageLedger deferred_ledger(deferred_model,
                                                      deferred_store);
    assert(deferred_ledger.recordOutbound(
               outgoing(600, chat::MessageStatus::Queued),
               true) == chat::delivery::LedgerPersistence::Deferred);
    assert(!deferred_store.getMessage(600, &stored));
    std::size_t deferred_total = 0;
    const chat::ConversationId deferred_conversation(
        chat::ChannelId::PRIMARY,
        0xAABBCCDD,
        chat::MeshProtocol::Meshtastic);
    auto deferred_page = deferred_ledger.loadPageFromLatest(
        deferred_conversation,
        0,
        10,
        &deferred_total);
    assert(deferred_total == 1);
    assert(deferred_page.size() == 1);
    assert(deferred_page.front().msg_id == 600);
    auto deferred_conversations = deferred_ledger.loadConversationPage(
        chat::MeshProtocol::Meshtastic,
        0,
        10,
        &deferred_total);
    assert(deferred_total == 1);
    assert(deferred_conversations.size() == 1);
    assert(deferred_conversations.front().id == deferred_conversation);
    assert(deferred_conversations.front().preview == "ledger");
    assert(deferred_ledger.applyOutboundStatusForProtocol(
        600,
        chat::MeshProtocol::Meshtastic,
        chat::MessageStatus::Delivered,
        true));
    assert(deferred_ledger.flushPendingWrites(1) == 1);
    assert(deferred_store.getMessageForProtocol(
        600,
        chat::MeshProtocol::Meshtastic,
        &stored));
    assert(stored.status == chat::MessageStatus::Delivered);
    deferred_page = deferred_ledger.loadPageFromLatest(
        deferred_conversation,
        0,
        10,
        &deferred_total);
    assert(deferred_total == 1);
    assert(deferred_page.size() == 1);

    deferred_store.status_failures = 1;
    deferred_ledger.recordOutbound(
        outgoing(601, chat::MessageStatus::Queued),
        true);
    assert(deferred_ledger.applyOutboundStatusForProtocol(
        601,
        chat::MeshProtocol::Meshtastic,
        chat::MessageStatus::Delivered,
        true));
    assert(deferred_store.getMessageForProtocol(
        601,
        chat::MeshProtocol::Meshtastic,
        &stored));
    assert(stored.status == chat::MessageStatus::Queued);
    assert(deferred_ledger.findMessageForProtocol(
        601,
        chat::MeshProtocol::Meshtastic,
        stored));
    assert(stored.status == chat::MessageStatus::Delivered);
    assert(deferred_ledger.flushPendingWrites(1) == 1);
    assert(deferred_store.getMessageForProtocol(
        601,
        chat::MeshProtocol::Meshtastic,
        &stored));
    assert(stored.status == chat::MessageStatus::Delivered);

    chat::ChatModel background_model;
    TransientStore background_store;
    chat::delivery::ChatMessageLedger background_ledger(background_model,
                                                        background_store);
    assert(background_ledger.recordOutbound(
               outgoing(700, chat::MessageStatus::Queued),
               true) == chat::delivery::LedgerPersistence::Durable);
    background_model.clearAll();
    background_store.protocol_lookup_count = 0;
    assert(background_ledger.applyOutboundStatusForProtocol(
        700,
        chat::MeshProtocol::Meshtastic,
        chat::MessageStatus::Failed,
        true));
    assert(background_store.protocol_lookup_count == 0);
    assert(background_store.getMessageForProtocol(
        700,
        chat::MeshProtocol::Meshtastic,
        &stored));
    assert(stored.status == chat::MessageStatus::Failed);

    assert(background_ledger.recordOutbound(
               outgoing(701, chat::MessageStatus::Queued),
               true) == chat::delivery::LedgerPersistence::Durable);
    background_model.clearAll();
    background_store.protocol_lookup_count = 0;
    assert(background_ledger.applyOutboundStatus(
        701,
        chat::MessageStatus::Failed,
        true));
    assert(background_store.protocol_lookup_count == 0);

    // Zero numeric peer is normal for full-destination Reticulum private text.
    // Test both durable projections and the ledger's transient-write overlay.
    chat::ChatModel reticulum_model;
    TransientStore reticulum_store;
    chat::delivery::ChatMessageLedger reticulum_ledger(reticulum_model, reticulum_store);
    auto private_text = outgoing(800, chat::MessageStatus::Queued);
    private_text.timestamp = 800;
    private_text.protocol = chat::MeshProtocol::Reticulum;
    private_text.peer = 0;
    private_text.reticulum_identity.valid = true;
    private_text.reticulum_identity.destination_hash[0] = 0x12;
    private_text.reticulum_identity.destination_hash[15] = 0xAB;
    const std::string expected_name = "RT 120000000000000000000000000000AB";
    reticulum_store.append_failures = 1;
    assert(reticulum_ledger.recordOutbound(private_text, true) == chat::delivery::LedgerPersistence::Deferred);
    assert(reticulum_model.getConversations().size() == 1);
    assert(reticulum_model.getConversations().front().name == expected_name);
    assert(reticulum_ledger.loadConversationPage(chat::MeshProtocol::Reticulum, 0, 10, nullptr).front().name == expected_name);
    assert(reticulum_ledger.flushPendingWrites(1) == 1);
    assert(reticulum_store.loadConversationPage(0, 10, nullptr).front().name == expected_name);
    assert(reticulum_ledger.loadConversationPage(chat::MeshProtocol::Reticulum, 0, 10, nullptr).front().name == expected_name);
    private_text.reticulum_identity.destination_hash[15] = 0xAC;
    assert(chat::fallbackConversationName(chat::conversationIdForMessage(private_text)) != expected_name);
    assert(chat::fallbackConversationName({chat::ChannelId::PRIMARY, 0, chat::MeshProtocol::Meshtastic}) == "Broadcast");
    assert(chat::fallbackConversationName({chat::ChannelId::PRIMARY, 0x12345678, chat::MeshProtocol::MeshCore}) == "5678");
    return 0;
}

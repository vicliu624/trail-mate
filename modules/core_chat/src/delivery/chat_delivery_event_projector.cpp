#include "chat/delivery/chat_delivery_event_projector.h"

namespace chat::delivery
{

DeliveryFailureKind toDeliveryFailureKind(SendFailureKind failure)
{
    switch (failure)
    {
    case SendFailureKind::None:
        return DeliveryFailureKind::None;
    case SendFailureKind::PeerKeyMissing:
        return DeliveryFailureKind::PeerKeyMissing;
    case SendFailureKind::LocalIdentityMissing:
        return DeliveryFailureKind::LocalIdentityMissing;
    case SendFailureKind::RadioSendFailed:
        return DeliveryFailureKind::RadioSendFailed;
    case SendFailureKind::AckTimeout:
        return DeliveryFailureKind::AckTimeout;
    case SendFailureKind::UnsupportedProtocol:
        return DeliveryFailureKind::UnsupportedProtocol;
    case SendFailureKind::Rejected:
        return DeliveryFailureKind::Rejected;
    case SendFailureKind::Unknown:
        return DeliveryFailureKind::Unknown;
    }
    return DeliveryFailureKind::Unknown;
}

ChatDeliveryEventProjector::ChatDeliveryEventProjector(
    ChatDeliveryReadModel& read_model)
    : read_model_(read_model)
{
}

void ChatDeliveryEventProjector::onQueued(ChatDeliveryRef ref, uint32_t now_ms)
{
    project(ref, DeliveryState::Queued, DeliveryFailureKind::None, now_ms);
}

void ChatDeliveryEventProjector::onSending(ChatDeliveryRef ref, uint32_t now_ms)
{
    project(ref, DeliveryState::Sending, DeliveryFailureKind::None, now_ms);
}

void ChatDeliveryEventProjector::onSent(ChatDeliveryRef ref, uint32_t now_ms)
{
    project(ref, DeliveryState::Sent, DeliveryFailureKind::None, now_ms);
}

void ChatDeliveryEventProjector::onDelivered(ChatDeliveryRef ref,
                                             uint32_t now_ms)
{
    project(ref, DeliveryState::Delivered, DeliveryFailureKind::None, now_ms);
}

void ChatDeliveryEventProjector::onFailed(ChatDeliveryRef ref,
                                          SendFailureKind failure,
                                          uint32_t now_ms)
{
    project(ref,
            DeliveryState::Failed,
            toDeliveryFailureKind(failure),
            now_ms);
}

void ChatDeliveryEventProjector::onReceived(ChatDeliveryRef ref,
                                            uint32_t now_ms)
{
    project(ref, DeliveryState::Received, DeliveryFailureKind::None, now_ms);
}

void ChatDeliveryEventProjector::project(ChatDeliveryRef ref,
                                         DeliveryState state,
                                         DeliveryFailureKind failure,
                                         uint32_t now_ms)
{
    ChatDeliveryRecord record{};
    record.ref = ref;
    record.state = state;
    record.failure = failure;
    record.updated_at_ms = now_ms;
    (void)read_model_.upsert(record);
}

} // namespace chat::delivery

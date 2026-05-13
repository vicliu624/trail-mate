#include "chat/delivery/legacy_chat_delivery_bridge.h"

namespace chat::delivery
{
namespace
{

DeliveryState mapStatus(chat::MessageStatus status)
{
    switch (status)
    {
    case chat::MessageStatus::Incoming:
        return DeliveryState::Received;
    case chat::MessageStatus::Queued:
        return DeliveryState::Queued;
    case chat::MessageStatus::Sent:
        return DeliveryState::Sent;
    case chat::MessageStatus::Failed:
        return DeliveryState::Failed;
    }
    return DeliveryState::Unknown;
}

DeliveryFailureKind mapFailure(chat::MessageStatus status)
{
    return status == chat::MessageStatus::Failed
               ? DeliveryFailureKind::Unknown
               : DeliveryFailureKind::None;
}

} // namespace

ChatDeliveryRef toDeliveryRef(const chat::ChatMessage& message)
{
    ChatDeliveryRef ref{};
    ref.protocol_id = message.msg_id;
    return ref;
}

ChatDeliveryRecord toDeliveryRecord(const chat::ChatMessage& message,
                                    uint32_t now_ms)
{
    ChatDeliveryRecord record{};
    record.ref = toDeliveryRef(message);
    record.state = mapStatus(message.status);
    record.failure = mapFailure(message.status);
    record.updated_at_ms = now_ms;
    return record;
}

ChatDeliveryRecord toFailedDeliveryRecord(ChatDeliveryRef ref,
                                          SendFailureKind failure,
                                          uint32_t now_ms)
{
    ChatDeliveryRecord record{};
    record.ref = ref;
    record.state = DeliveryState::Failed;
    record.failure = toDeliveryFailureKind(failure);
    record.updated_at_ms = now_ms;
    return record;
}

} // namespace chat::delivery

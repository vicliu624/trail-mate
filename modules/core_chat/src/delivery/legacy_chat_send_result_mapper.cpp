#include "chat/delivery/legacy_chat_send_result_mapper.h"

namespace chat::delivery
{

SendFailureKind mapLegacyChatSendFailure(LegacyChatSendFailure failure)
{
    switch (failure)
    {
    case LegacyChatSendFailure::None:
        return SendFailureKind::None;
    case LegacyChatSendFailure::PeerKeyMissing:
        return SendFailureKind::PeerKeyMissing;
    case LegacyChatSendFailure::LocalIdentityMissing:
        return SendFailureKind::LocalIdentityMissing;
    case LegacyChatSendFailure::RadioSendFailed:
        return SendFailureKind::RadioSendFailed;
    case LegacyChatSendFailure::AckTimeout:
        return SendFailureKind::AckTimeout;
    case LegacyChatSendFailure::UnsupportedProtocol:
        return SendFailureKind::UnsupportedProtocol;
    case LegacyChatSendFailure::Rejected:
        return SendFailureKind::Rejected;
    case LegacyChatSendFailure::Unknown:
        return SendFailureKind::Unknown;
    }
    return SendFailureKind::Unknown;
}

ChatDeliveryEvent mapLegacyChatSendResult(
    const LegacyChatSendResult& result)
{
    ChatDeliveryEvent event{};
    event.ref = result.ref;
    event.timestamp_ms = result.timestamp_ms;
    if (result.success)
    {
        event.state = DeliveryState::Sent;
        event.failure = SendFailureKind::None;
        return event;
    }

    event.state = DeliveryState::Failed;
    event.failure = mapLegacyChatSendFailure(result.failure);
    if (event.failure == SendFailureKind::None)
    {
        event.failure = SendFailureKind::Unknown;
    }
    return event;
}

ChatDeliveryEvent makeAckTimeoutDeliveryEvent(ChatDeliveryRef ref,
                                              uint32_t timestamp_ms)
{
    LegacyChatSendResult result{};
    result.ref = ref;
    result.success = false;
    result.failure = LegacyChatSendFailure::AckTimeout;
    result.timestamp_ms = timestamp_ms;
    return mapLegacyChatSendResult(result);
}

} // namespace chat::delivery

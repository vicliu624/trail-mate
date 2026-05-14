#pragma once

#include "chat/delivery/chat_delivery_event_port.h"

namespace chat::delivery
{

enum class LegacyChatSendFailure : uint8_t
{
    None,
    PeerKeyMissing,
    LocalIdentityMissing,
    RadioSendFailed,
    AckTimeout,
    UnsupportedProtocol,
    Rejected,
    Unknown,
};

struct LegacyChatSendResult
{
    ChatDeliveryRef ref;
    bool success = false;
    LegacyChatSendFailure failure = LegacyChatSendFailure::Unknown;
    uint32_t timestamp_ms = 0;
};

SendFailureKind mapLegacyChatSendFailure(LegacyChatSendFailure failure);
ChatDeliveryEvent mapLegacyChatSendResult(
    const LegacyChatSendResult& result);
ChatDeliveryEvent makeAckTimeoutDeliveryEvent(ChatDeliveryRef ref,
                                              uint32_t timestamp_ms = 0);

} // namespace chat::delivery

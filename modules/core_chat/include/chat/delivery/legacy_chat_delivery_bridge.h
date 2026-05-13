#pragma once

#include "chat/delivery/chat_delivery_event_projector.h"
#include "chat/domain/chat_types.h"

namespace chat::delivery
{

ChatDeliveryRef toDeliveryRef(const chat::ChatMessage& message);
ChatDeliveryRecord toDeliveryRecord(const chat::ChatMessage& message,
                                    uint32_t now_ms = 0);
ChatDeliveryRecord toFailedDeliveryRecord(ChatDeliveryRef ref,
                                          SendFailureKind failure,
                                          uint32_t now_ms = 0);

} // namespace chat::delivery

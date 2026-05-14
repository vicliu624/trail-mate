#pragma once

#include "chat/delivery/chat_delivery_types.h"

namespace chat::delivery
{

enum class ChatDeliveryActionKind : uint8_t
{
    Retry,
    CancelPending,
    ClearFailure,
};

struct ChatDeliveryActionRequest
{
    ChatDeliveryActionKind kind = ChatDeliveryActionKind::ClearFailure;
    ChatDeliveryRef ref;
};

enum class ChatDeliveryActionFailure : uint8_t
{
    None,
    InvalidRef,
    NotFound,
    Unsupported,
    NotRetryable,
    Rejected,
};

struct ChatDeliveryActionResult
{
    bool ok = false;
    ChatDeliveryActionFailure failure = ChatDeliveryActionFailure::None;

    static ChatDeliveryActionResult success()
    {
        return {true, ChatDeliveryActionFailure::None};
    }

    static ChatDeliveryActionResult fail(ChatDeliveryActionFailure failure)
    {
        return {false, failure};
    }
};

} // namespace chat::delivery

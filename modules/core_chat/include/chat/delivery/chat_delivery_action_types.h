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
        ChatDeliveryActionResult result;
        result.ok = true;
        result.failure = ChatDeliveryActionFailure::None;
        return result;
    }

    static ChatDeliveryActionResult fail(ChatDeliveryActionFailure failure)
    {
        ChatDeliveryActionResult result;
        result.ok = false;
        result.failure = failure;
        return result;
    }
};

} // namespace chat::delivery

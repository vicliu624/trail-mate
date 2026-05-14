#include "chat/delivery/chat_delivery_action_service.h"

namespace chat::delivery
{

ChatDeliveryActionService::ChatDeliveryActionService(
    ChatDeliveryReadModel& read_model,
    IRetryChatMessagePort* retry_port)
    : read_model_(read_model),
      retry_port_(retry_port)
{
}

ChatDeliveryActionResult ChatDeliveryActionService::handleDeliveryAction(
    const ChatDeliveryActionRequest& request)
{
    if (!request.ref.isValid())
    {
        return ChatDeliveryActionResult::fail(
            ChatDeliveryActionFailure::InvalidRef);
    }

    switch (request.kind)
    {
    case ChatDeliveryActionKind::Retry:
        return retry(request.ref);
    case ChatDeliveryActionKind::CancelPending:
        return cancelPending(request.ref);
    case ChatDeliveryActionKind::ClearFailure:
        return clearFailure(request.ref);
    }

    return ChatDeliveryActionResult::fail(
        ChatDeliveryActionFailure::Unsupported);
}

ChatDeliveryActionResult ChatDeliveryActionService::retry(ChatDeliveryRef ref)
{
    if (retry_port_ == nullptr)
    {
        return ChatDeliveryActionResult::fail(
            ChatDeliveryActionFailure::Unsupported);
    }
    return retry_port_->retryMessage(ref);
}

ChatDeliveryActionResult ChatDeliveryActionService::cancelPending(
    ChatDeliveryRef ref)
{
    ChatDeliveryRecord record{};
    if (!read_model_.find(ref, record))
    {
        return ChatDeliveryActionResult::fail(
            ChatDeliveryActionFailure::NotFound);
    }

    if (record.state != DeliveryState::Queued &&
        record.state != DeliveryState::Sending)
    {
        return ChatDeliveryActionResult::fail(
            ChatDeliveryActionFailure::NotRetryable);
    }

    read_model_.clear(ref);
    return ChatDeliveryActionResult::success();
}

ChatDeliveryActionResult ChatDeliveryActionService::clearFailure(
    ChatDeliveryRef ref)
{
    ChatDeliveryRecord record{};
    if (!read_model_.find(ref, record))
    {
        return ChatDeliveryActionResult::fail(
            ChatDeliveryActionFailure::NotFound);
    }

    if (record.state != DeliveryState::Failed)
    {
        return ChatDeliveryActionResult::fail(
            ChatDeliveryActionFailure::NotRetryable);
    }

    read_model_.clear(ref);
    return ChatDeliveryActionResult::success();
}

} // namespace chat::delivery

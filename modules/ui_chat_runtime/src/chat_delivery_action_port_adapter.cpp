#include "ui_chat_runtime/chat_delivery_action_port_adapter.h"

namespace ui_chat_runtime
{

ChatDeliveryActionPortAdapter::ChatDeliveryActionPortAdapter(
    IChatDeliveryActionPort& action_port)
    : action_port_(action_port)
{
}

ChatDeliveryActionResult
ChatDeliveryActionPortAdapter::handleMessageAction(
    ::chat::delivery::ChatDeliveryActionKind kind,
    const ::ui::chat::MessageRef& ref)
{
    ::chat::delivery::ChatDeliveryActionRequest request{};
    request.kind = kind;
    request.ref = toDeliveryRef(ref);
    return action_port_.handleDeliveryAction(request);
}

ChatDeliveryActionResult ChatDeliveryActionPortAdapter::retryMessage(
    const ::ui::chat::MessageRef& ref)
{
    return handleMessageAction(
        ::chat::delivery::ChatDeliveryActionKind::Retry,
        ref);
}

ChatDeliveryActionResult ChatDeliveryActionPortAdapter::cancelPending(
    const ::ui::chat::MessageRef& ref)
{
    return handleMessageAction(
        ::chat::delivery::ChatDeliveryActionKind::CancelPending,
        ref);
}

ChatDeliveryActionResult ChatDeliveryActionPortAdapter::clearFailure(
    const ::ui::chat::MessageRef& ref)
{
    return handleMessageAction(
        ::chat::delivery::ChatDeliveryActionKind::ClearFailure,
        ref);
}

::chat::delivery::ChatDeliveryRef toDeliveryRef(
    const ::ui::chat::MessageRef& ref)
{
    ::chat::delivery::ChatDeliveryRef out{};
    out.local_id = ref.local_id;
    out.protocol_id = ref.protocol_id;
    out.nonce_or_seq = ref.nonce_or_seq;
    return out;
}

} // namespace ui_chat_runtime

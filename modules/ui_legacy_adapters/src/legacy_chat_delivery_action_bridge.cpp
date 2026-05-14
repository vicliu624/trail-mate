#include "ui_legacy_adapters/legacy_chat_delivery_action_bridge.h"

namespace ui::presentation_sources
{

LegacyChatDeliveryActionBridge::LegacyChatDeliveryActionBridge(
    ::chat::delivery::IChatDeliveryActionSink& action_sink)
    : action_sink_(action_sink)
{
}

::chat::delivery::ChatDeliveryActionResult
LegacyChatDeliveryActionBridge::handleMessageAction(
    ::chat::delivery::ChatDeliveryActionKind kind,
    const ::ui::chat::MessageRef& ref)
{
    ::chat::delivery::ChatDeliveryActionRequest request{};
    request.kind = kind;
    request.ref = toDeliveryRef(ref);
    return action_sink_.handleDeliveryAction(request);
}

::chat::delivery::ChatDeliveryActionResult
LegacyChatDeliveryActionBridge::retryMessage(
    const ::ui::chat::MessageRef& ref)
{
    return handleMessageAction(
        ::chat::delivery::ChatDeliveryActionKind::Retry,
        ref);
}

::chat::delivery::ChatDeliveryActionResult
LegacyChatDeliveryActionBridge::cancelPending(
    const ::ui::chat::MessageRef& ref)
{
    return handleMessageAction(
        ::chat::delivery::ChatDeliveryActionKind::CancelPending,
        ref);
}

::chat::delivery::ChatDeliveryActionResult
LegacyChatDeliveryActionBridge::clearFailure(
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

} // namespace ui::presentation_sources

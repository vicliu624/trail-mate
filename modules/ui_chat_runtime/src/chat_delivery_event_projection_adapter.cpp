#include "ui_chat_runtime/chat_delivery_event_projection_adapter.h"

#include "chat/delivery/legacy_chat_delivery_bridge.h"
#include "chat/usecase/chat_service.h"
#include "sys/event_bus.h"

namespace ui_chat_runtime
{

ChatDeliveryEventProjectionAdapter::ChatDeliveryEventProjectionAdapter(
    ::chat::ChatService& chat_service,
    IChatDeliveryEventPort& delivery_events)
    : chat_service_(chat_service),
      delivery_events_(delivery_events)
{
}

void ChatDeliveryEventProjectionAdapter::onChatSendResult(
    const ::sys::ChatSendResultEvent& event)
{
    const auto failure = event.success
                             ? ::chat::delivery::LegacyChatSendFailure::None
                             : ::chat::delivery::LegacyChatSendFailure::Unknown;
    (void)publishSendResult(event.msg_id,
                            event.success,
                            failure,
                            event.timestamp);
}

void ChatDeliveryEventProjectionAdapter::onAckTimeout(
    ::chat::MessageId msg_id,
    uint32_t timestamp_ms)
{
    (void)publishSendResult(
        msg_id,
        false,
        ::chat::delivery::LegacyChatSendFailure::AckTimeout,
        timestamp_ms);
}

bool ChatDeliveryEventProjectionAdapter::publishSendResult(
    ::chat::MessageId msg_id,
    bool success,
    ::chat::delivery::LegacyChatSendFailure failure,
    uint32_t timestamp_ms)
{
    if (msg_id == 0)
    {
        return false;
    }

    const ::chat::ChatMessage* message = chat_service_.getMessage(msg_id);
    if (message == nullptr)
    {
        return false;
    }

    ::chat::delivery::LegacyChatSendResult result{};
    result.ref = ::chat::delivery::toDeliveryRef(*message);
    result.success = success;
    result.failure = failure;
    result.timestamp_ms = timestamp_ms;
    delivery_events_.publishDeliveryEvent(
        ::chat::delivery::mapLegacyChatSendResult(result));
    return true;
}

} // namespace ui_chat_runtime

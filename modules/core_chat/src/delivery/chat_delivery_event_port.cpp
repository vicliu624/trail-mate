#include "chat/delivery/chat_delivery_event_port.h"

namespace chat::delivery
{

ProjectingChatDeliveryEventPort::ProjectingChatDeliveryEventPort(
    ChatDeliveryEventProjector& projector)
    : projector_(projector)
{
}

void ProjectingChatDeliveryEventPort::publishDeliveryEvent(
    const ChatDeliveryEvent& event)
{
    if (!event.ref.isValid())
    {
        return;
    }

    switch (event.state)
    {
    case DeliveryState::Queued:
        projector_.onQueued(event.ref, event.timestamp_ms);
        break;
    case DeliveryState::Sending:
        projector_.onSending(event.ref, event.timestamp_ms);
        break;
    case DeliveryState::Sent:
        projector_.onSent(event.ref, event.timestamp_ms);
        break;
    case DeliveryState::Delivered:
        projector_.onDelivered(event.ref, event.timestamp_ms);
        break;
    case DeliveryState::Failed:
        projector_.onFailed(event.ref, event.failure, event.timestamp_ms);
        break;
    case DeliveryState::Received:
        projector_.onReceived(event.ref, event.timestamp_ms);
        break;
    case DeliveryState::Unknown:
        break;
    }
}

} // namespace chat::delivery

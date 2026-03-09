#include "platform/esp/arduino_common/chat/infra/chat_event_bus_bridge.h"

#include "sys/event_bus.h"

namespace chat::infra
{

ChatEventBusBridge::ChatEventBusBridge(chat::ChatService& service) : service_(service)
{
    service_.addIncomingMessageObserver(this);
}

ChatEventBusBridge::~ChatEventBusBridge()
{
    service_.removeIncomingMessageObserver(this);
}

void ChatEventBusBridge::onIncomingMessage(const chat::ChatMessage& msg, const chat::RxMeta* rx_meta)
{
    sys::EventBus::publish(new sys::ChatNewMessageEvent(static_cast<uint8_t>(msg.channel),
                                                        msg.msg_id,
                                                        msg.text.c_str(),
                                                        rx_meta));
}

} // namespace chat::infra

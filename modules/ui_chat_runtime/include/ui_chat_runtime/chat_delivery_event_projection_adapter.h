#pragma once

#include "chat/delivery/legacy_chat_send_result_mapper.h"
#include "chat/domain/chat_types.h"
#include "ui_chat_runtime/chat_delivery_event_port.h"

namespace chat
{
class ChatService;
}

namespace sys
{
struct ChatSendResultEvent;
}

namespace ui_chat_runtime
{

class ChatDeliveryEventProjectionAdapter
{
  public:
    ChatDeliveryEventProjectionAdapter(
        ::chat::ChatService& chat_service,
        IChatDeliveryEventPort& delivery_events);

    void onChatSendResult(const ::sys::ChatSendResultEvent& event);
    void onAckTimeout(::chat::MessageId msg_id, uint32_t timestamp_ms = 0);

  private:
    bool publishSendResult(::chat::MessageId msg_id,
                           bool success,
                           ::chat::delivery::LegacyChatSendFailure failure,
                           uint32_t timestamp_ms);

    ::chat::ChatService& chat_service_;
    IChatDeliveryEventPort& delivery_events_;
};

} // namespace ui_chat_runtime

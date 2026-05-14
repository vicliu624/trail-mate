#pragma once

#include "chat/delivery/chat_delivery_event_port.h"
#include "chat/delivery/legacy_chat_send_result_mapper.h"
#include "chat/domain/chat_types.h"

namespace chat
{
class ChatService;
}

namespace sys
{
struct ChatSendResultEvent;
}

namespace ui::presentation_sources
{

// LegacyChatDeliveryEventBridge is a Phase 7.3 anti-corruption bridge.
//
// Pattern:
//   Runtime Event Bridge / Event Projection Adapter.
//
// It translates existing chat send-result events into ChatDeliveryEventPort
// calls. It must not render UI, build ChatWorkspaceSnapshot, retry messages, or
// send packets.
class LegacyChatDeliveryEventBridge
{
  public:
    LegacyChatDeliveryEventBridge(
        ::chat::ChatService& chat_service,
        ::chat::delivery::IChatDeliveryEventPort& delivery_events);

    void onChatSendResult(const ::sys::ChatSendResultEvent& event);
    void onAckTimeout(::chat::MessageId msg_id, uint32_t timestamp_ms = 0);

  private:
    bool publishSendResult(::chat::MessageId msg_id,
                           bool success,
                           ::chat::delivery::LegacyChatSendFailure failure,
                           uint32_t timestamp_ms);

    ::chat::ChatService& chat_service_;
    ::chat::delivery::IChatDeliveryEventPort& delivery_events_;
};

} // namespace ui::presentation_sources

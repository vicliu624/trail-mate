#pragma once

#include "chat/delivery/chat_delivery_types.h"
#include "ui_chat_runtime/chat_delivery_action_port.h"
#include "ui_presentation/chat/chat_message_ref.h"

namespace ui_chat_runtime
{

class ChatDeliveryActionPortAdapter
{
  public:
    explicit ChatDeliveryActionPortAdapter(
        IChatDeliveryActionPort& action_port);

    ChatDeliveryActionResult handleMessageAction(
        ::chat::delivery::ChatDeliveryActionKind kind,
        const ::ui::chat::MessageRef& ref);

    ChatDeliveryActionResult retryMessage(const ::ui::chat::MessageRef& ref);
    ChatDeliveryActionResult cancelPending(const ::ui::chat::MessageRef& ref);
    ChatDeliveryActionResult clearFailure(const ::ui::chat::MessageRef& ref);

  private:
    IChatDeliveryActionPort& action_port_;
};

::chat::delivery::ChatDeliveryRef toDeliveryRef(
    const ::ui::chat::MessageRef& ref);

} // namespace ui_chat_runtime

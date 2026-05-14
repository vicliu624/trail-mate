#pragma once

#include "chat/delivery/chat_delivery_action_types.h"

namespace chat::delivery
{

class IChatDeliveryActionSink
{
  public:
    virtual ~IChatDeliveryActionSink() = default;

    virtual ChatDeliveryActionResult handleDeliveryAction(
        const ChatDeliveryActionRequest& request) = 0;
};

} // namespace chat::delivery

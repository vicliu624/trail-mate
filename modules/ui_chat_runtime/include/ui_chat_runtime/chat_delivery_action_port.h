#pragma once

#include "chat/delivery/chat_delivery_action_sink.h"
#include "chat/delivery/chat_delivery_action_types.h"

namespace ui_chat_runtime
{

using ChatDeliveryActionRequest =
    ::chat::delivery::ChatDeliveryActionRequest;
using ChatDeliveryActionResult =
    ::chat::delivery::ChatDeliveryActionResult;
using IChatDeliveryActionPort =
    ::chat::delivery::IChatDeliveryActionSink;

} // namespace ui_chat_runtime

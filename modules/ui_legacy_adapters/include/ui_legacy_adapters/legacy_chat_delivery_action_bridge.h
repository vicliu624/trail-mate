#pragma once

#include "ui_chat_runtime/chat_delivery_action_port_adapter.h"

namespace ui::presentation_sources
{

using LegacyChatDeliveryActionBridge
    [[deprecated("Use ui_chat_runtime::ChatDeliveryActionPortAdapter")]] =
        ::ui_chat_runtime::ChatDeliveryActionPortAdapter;
using ::ui_chat_runtime::toDeliveryRef;

} // namespace ui::presentation_sources

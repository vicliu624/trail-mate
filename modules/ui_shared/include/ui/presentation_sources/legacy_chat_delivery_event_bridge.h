#pragma once

#include "ui_chat_runtime/chat_delivery_event_projection_adapter.h"

namespace ui::presentation_sources
{

using LegacyChatDeliveryEventBridge
    [[deprecated("Use ui_chat_runtime::ChatDeliveryEventProjectionAdapter")]] =
        ::ui_chat_runtime::ChatDeliveryEventProjectionAdapter;

} // namespace ui::presentation_sources

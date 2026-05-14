#pragma once

#include "chat/delivery/chat_delivery_action_sink.h"
#include "ui_presentation/chat/chat_message_ref.h"

namespace ui::presentation_sources
{

// LegacyChatDeliveryActionBridge is a Phase 7.4 anti-corruption bridge.
//
// Pattern:
//   Command Bridge / Runtime Action Adapter.
//
// It translates compatibility UI message references into chat delivery action
// requests. It must not render UI, build snapshots, retry messages, or mutate
// the delivery read model directly.
class LegacyChatDeliveryActionBridge
{
  public:
    explicit LegacyChatDeliveryActionBridge(
        ::chat::delivery::IChatDeliveryActionSink& action_sink);

    ::chat::delivery::ChatDeliveryActionResult handleMessageAction(
        ::chat::delivery::ChatDeliveryActionKind kind,
        const ::ui::chat::MessageRef& ref);

    ::chat::delivery::ChatDeliveryActionResult retryMessage(
        const ::ui::chat::MessageRef& ref);
    ::chat::delivery::ChatDeliveryActionResult cancelPending(
        const ::ui::chat::MessageRef& ref);
    ::chat::delivery::ChatDeliveryActionResult clearFailure(
        const ::ui::chat::MessageRef& ref);

  private:
    ::chat::delivery::IChatDeliveryActionSink& action_sink_;
};

::chat::delivery::ChatDeliveryRef toDeliveryRef(
    const ::ui::chat::MessageRef& ref);

} // namespace ui::presentation_sources

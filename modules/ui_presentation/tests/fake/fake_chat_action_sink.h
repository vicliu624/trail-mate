#pragma once

#include "ui_presentation/chat/chat_action_sink.h"

namespace ui::tests
{

class FakeChatActionSink final : public ui::chat::IChatActionSink
{
  public:
    ui::UiActionResult selectConversation(ui::chat::ConversationId id) override
    {
        ++select_count;
        last_selected = id;
        return select_result;
    }

    ui::UiActionResult sendMessage(const ui::chat::SendMessageView& message) override
    {
        ++send_count;
        last_message = message;
        return send_result;
    }

    ui::UiActionResult markRead(ui::chat::ConversationId id) override
    {
        ++mark_read_count;
        last_mark_read = id;
        return mark_read_result;
    }

    int select_count = 0;
    int send_count = 0;
    int mark_read_count = 0;

    ui::chat::ConversationId last_selected{};
    ui::chat::SendMessageView last_message{};
    ui::chat::ConversationId last_mark_read{};

    ui::UiActionResult select_result = ui::UiActionResult::success();
    ui::UiActionResult send_result = ui::UiActionResult::success();
    ui::UiActionResult mark_read_result = ui::UiActionResult::success();
};

} // namespace ui::tests

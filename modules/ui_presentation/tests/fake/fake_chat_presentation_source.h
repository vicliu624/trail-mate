#pragma once

#include "ui_presentation/chat/chat_presentation_source.h"

namespace ui::tests
{

class FakeChatPresentationSource final : public ui::chat::IChatPresentationSource
{
  public:
    bool buildChatWorkspaceSnapshot(ui::chat::ChatWorkspaceSnapshot& out,
                                    ui::chat::ConversationId selected,
                                    uint16_t conversation_offset,
                                    uint16_t message_offset) const override
    {
        ++build_count;
        last_selected = selected;
        last_conversation_offset = conversation_offset;
        last_message_offset = message_offset;

        if (!available)
        {
            return false;
        }

        out = snapshot_value;
        out.selected_conversation = selected;
        return true;
    }

    bool available = true;
    ui::chat::ChatWorkspaceSnapshot snapshot_value{};

    mutable int build_count = 0;
    mutable ui::chat::ConversationId last_selected{};
    mutable uint16_t last_conversation_offset = 0;
    mutable uint16_t last_message_offset = 0;
};

} // namespace ui::tests

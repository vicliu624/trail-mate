#pragma once

#include "ui_presentation/chat/chat_presentation_source.h"

namespace ui::tests
{

class FakeChatPresentationSource final : public ui::chat::IChatPresentationSource
{
  public:
    bool buildChatWorkspaceSnapshot(
        const ui::chat::ChatWorkspaceRequest& request,
        ui::chat::ChatWorkspaceSnapshot& out) const override
    {
        ++build_count;
        last_request = request;

        if (!available)
        {
            return false;
        }

        out = snapshot_value;
        out.selected_conversation = request.selected;
        return true;
    }

    bool available = true;
    ui::chat::ChatWorkspaceSnapshot snapshot_value{};

    mutable int build_count = 0;
    mutable ui::chat::ChatWorkspaceRequest last_request{};
};

} // namespace ui::tests

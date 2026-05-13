#pragma once

#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "ui_presentation/chat/chat_presentation_source.h"

namespace ui::presentation_sources
{

class LegacyChatPresentationSource final : public ui::chat::IChatPresentationSource
{
  public:
    LegacyChatPresentationSource(::chat::ChatService& chat_service,
                                 ::chat::contacts::ContactService* contact_service = nullptr);

    bool buildChatWorkspaceSnapshot(
        const ui::chat::ChatWorkspaceRequest& request,
        ui::chat::ChatWorkspaceSnapshot& out) const override;

  private:
    ::chat::ChatService& chat_service_;
    ::chat::contacts::ContactService* contact_service_ = nullptr;
};

} // namespace ui::presentation_sources

#pragma once

#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "ui_presentation/chat/chat_presentation_source.h"

namespace ui::presentation_sources
{

// LegacyChatPresentationSource is a Phase 5.6 compatibility read adapter.
//
// Pattern:
//   CQRS read model / projection / anti-corruption adapter.
//
// It may read ChatService and ContactService, and may use
// chat_presentation_adapters to map core chat types into ui_presentation rows.
//
// It must not:
//   - send messages
//   - mark conversations read
//   - mutate ChatService
//   - access LVGL widgets
//   - access radio, mesh adapter, PKI, or packet builders
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

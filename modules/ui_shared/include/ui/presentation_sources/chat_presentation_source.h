#pragma once

#include "chat/delivery/chat_delivery_read_model.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "ui_presentation/chat/chat_presentation_source.h"

namespace ui::presentation_sources
{

// ChatPresentationSource is the product chat read projection adapter.
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
class ChatPresentationSource final : public ui::chat::IChatPresentationSource
{
  public:
    ChatPresentationSource(::chat::ChatService& chat_service,
                           ::chat::contacts::ContactService* contact_service = nullptr,
                           const ::chat::delivery::ChatDeliveryReadModel*
                               delivery_read_model = nullptr);

    bool buildChatWorkspaceSnapshot(
        const ui::chat::ChatWorkspaceRequest& request,
        ui::chat::ChatWorkspaceSnapshot& out) const override;

  private:
    ::chat::ChatService& chat_service_;
    ::chat::contacts::ContactService* contact_service_ = nullptr;
    const ::chat::delivery::ChatDeliveryReadModel* delivery_read_model_ = nullptr;
};

} // namespace ui::presentation_sources

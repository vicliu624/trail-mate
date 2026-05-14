#pragma once

#include "chat/domain/chat_types.h"
#include "chat/usecase/chat_service.h"
#include "sys/event_bus.h"
#include "ui/screens/chat/chat_protocol_support.h"
#include "ui/screens/chat/chat_ui_refresh_sink.h"
#include "ui_presentation/key_verification/key_verification_model.h"

namespace ui::presentation_sources
{
class LegacyChatDeliveryEventBridge;
class LegacyKeyVerificationSource;
}

namespace chat::ui
{

class ChatPageRuntimeEventPump
{
  public:
    ChatPageRuntimeEventPump(
        chat::ChatService& service,
        ::ui::presentation_sources::LegacyChatDeliveryEventBridge*
            delivery_bridge,
        ::ui::presentation_sources::LegacyKeyVerificationSource*
            key_verification_source,
        ::ui::key_verification::KeyVerificationModel* key_verification_model,
        IChatUiRefreshSink* ui);

    void update();
    void handleEvent(sys::Event* event);

  private:
    void handleChatNewMessage(const sys::ChatNewMessageEvent& event);
    void handleChatSendResult(const sys::ChatSendResultEvent& event);
    void handleUnreadChanged();
    void handleKeyVerificationNumberRequest(
        const sys::KeyVerificationNumberRequestEvent& event);
    void handleKeyVerificationNumberInform(
        const sys::KeyVerificationNumberInformEvent& event);
    void handleKeyVerificationFinal(const sys::KeyVerificationFinalEvent& event);

    void showKeyVerificationForPeer(chat::MeshProtocol protocol,
                                    uint32_t peer_id);

    chat::ChatService& service_;
    ::ui::presentation_sources::LegacyChatDeliveryEventBridge*
        delivery_bridge_ = nullptr;
    ::ui::presentation_sources::LegacyKeyVerificationSource*
        key_verification_source_ = nullptr;
    ::ui::key_verification::KeyVerificationModel* key_verification_model_ =
        nullptr;
    IChatUiRefreshSink* ui_ = nullptr;
};

} // namespace chat::ui

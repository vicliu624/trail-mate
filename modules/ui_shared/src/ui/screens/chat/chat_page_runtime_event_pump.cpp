#include "ui/screens/chat/chat_page_runtime_event_pump.h"

#include "ui/presentation_sources/legacy_chat_delivery_event_bridge.h"
#include "ui/presentation_sources/legacy_key_verification_source.h"

namespace chat::ui
{

namespace
{

::ui::key_verification::VerificationProtocol toVerificationProtocol(
    chat::MeshProtocol protocol)
{
    switch (protocol)
    {
    case chat::MeshProtocol::MeshCore:
        return ::ui::key_verification::VerificationProtocol::MeshCore;
    case chat::MeshProtocol::Meshtastic:
        return ::ui::key_verification::VerificationProtocol::Meshtastic;
    case chat::MeshProtocol::RNode:
    case chat::MeshProtocol::LXMF:
    default:
        return ::ui::key_verification::VerificationProtocol::Unknown;
    }
}

} // namespace

ChatPageRuntimeEventPump::ChatPageRuntimeEventPump(
    chat::ChatService& service,
    ::ui::presentation_sources::LegacyChatDeliveryEventBridge* delivery_bridge,
    ::ui::presentation_sources::LegacyKeyVerificationSource*
        key_verification_source,
    ::ui::key_verification::KeyVerificationModel* key_verification_model,
    IChatUiRefreshSink* ui)
    : service_(service),
      delivery_bridge_(delivery_bridge),
      key_verification_source_(key_verification_source),
      key_verification_model_(key_verification_model),
      ui_(ui)
{
}

void ChatPageRuntimeEventPump::update()
{
    service_.processIncoming();
    service_.flushStore();
}

void ChatPageRuntimeEventPump::handleEvent(sys::Event* event)
{
    if (event == nullptr)
    {
        return;
    }

    switch (event->type)
    {
    case sys::EventType::ChatNewMessage:
        handleChatNewMessage(*static_cast<sys::ChatNewMessageEvent*>(event));
        break;
    case sys::EventType::ChatSendResult:
        handleChatSendResult(*static_cast<sys::ChatSendResultEvent*>(event));
        break;
    case sys::EventType::ChatUnreadChanged:
        handleUnreadChanged();
        break;
    case sys::EventType::KeyVerificationNumberRequest:
        handleKeyVerificationNumberRequest(
            *static_cast<sys::KeyVerificationNumberRequestEvent*>(event));
        break;
    case sys::EventType::KeyVerificationNumberInform:
        handleKeyVerificationNumberInform(
            *static_cast<sys::KeyVerificationNumberInformEvent*>(event));
        break;
    case sys::EventType::KeyVerificationFinal:
        handleKeyVerificationFinal(
            *static_cast<sys::KeyVerificationFinalEvent*>(event));
        break;
    default:
        break;
    }

    delete event;
}

void ChatPageRuntimeEventPump::handleChatNewMessage(
    const sys::ChatNewMessageEvent& event)
{
    if (ui_ != nullptr)
    {
        ui_->onRuntimeMessageArrived(event.msg_id);
    }
}

void ChatPageRuntimeEventPump::handleChatSendResult(
    const sys::ChatSendResultEvent& event)
{
    if (delivery_bridge_ != nullptr)
    {
        delivery_bridge_->onChatSendResult(event);
    }
    if (ui_ != nullptr)
    {
        ui_->onRuntimeSendResult(event.msg_id);
    }
}

void ChatPageRuntimeEventPump::handleUnreadChanged()
{
    if (ui_ != nullptr)
    {
        ui_->onRuntimeUnreadChanged();
    }
}

void ChatPageRuntimeEventPump::handleKeyVerificationNumberRequest(
    const sys::KeyVerificationNumberRequestEvent& event)
{
    if (key_verification_source_ != nullptr)
    {
        key_verification_source_->onNumberRequest(event.node_id, event.nonce);
    }
    showKeyVerificationForPeer(
        chat::ui::support::active_mesh_protocol(),
        event.node_id);
}

void ChatPageRuntimeEventPump::handleKeyVerificationNumberInform(
    const sys::KeyVerificationNumberInformEvent& event)
{
    if (key_verification_source_ != nullptr)
    {
        key_verification_source_->onNumberInform(event.node_id,
                                                event.nonce,
                                                event.security_number);
    }
    showKeyVerificationForPeer(
        chat::ui::support::active_mesh_protocol(),
        event.node_id);
}

void ChatPageRuntimeEventPump::handleKeyVerificationFinal(
    const sys::KeyVerificationFinalEvent& event)
{
    if (key_verification_source_ != nullptr)
    {
        key_verification_source_->onFinal(event.node_id,
                                         event.nonce,
                                         event.is_sender,
                                         event.verification_code);
    }
    showKeyVerificationForPeer(
        chat::ui::support::active_mesh_protocol(),
        event.node_id);
}

void ChatPageRuntimeEventPump::showKeyVerificationForPeer(
    chat::MeshProtocol protocol,
    uint32_t peer_id)
{
    if (key_verification_model_ == nullptr || ui_ == nullptr)
    {
        return;
    }

    key_verification_model_->selectPeer(toVerificationProtocol(protocol),
                                        peer_id);
    ui_->showKeyVerification(key_verification_model_->snapshot());
}

} // namespace chat::ui

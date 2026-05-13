#include "ui/presentation_sources/team_chat_action_sink.h"

#include "platform/ui/team_ui_store_runtime.h"
#include "sys/clock.h"
#include "team/protocol/team_chat.h"

#include <string>

namespace ui::presentation_sources
{
namespace
{

constexpr uint32_t kMinValidEpochSeconds = 1577836800U; // 2020-01-01
constexpr uint32_t kTeamComposeMsgIdStart = 1U;

bool isTeamConversation(ui::chat::ConversationId id)
{
    return id.isValid() && id.kind == ui::chat::ConversationKind::Team;
}

uint32_t currentTimestamp()
{
    uint32_t ts = sys::epoch_seconds_now();
    if (ts < kMinValidEpochSeconds)
    {
        ts = static_cast<uint32_t>(sys::millis_now() / 1000U);
    }
    return ts;
}

} // namespace

TeamChatActionSink::TeamChatActionSink(::team::ui::ITeamUiStore& team_store,
                                       ITeamChatCommandPort* command_port,
                                       uint8_t team_channel_raw)
    : team_store_(team_store),
      command_port_(command_port),
      team_channel_raw_(team_channel_raw)
{
}

ui::UiActionResult TeamChatActionSink::selectConversation(
    ui::chat::ConversationId id)
{
    if (!id.isValid())
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
    }
    if (!isTeamConversation(id))
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::Unsupported);
    }
    return ui::UiActionResult::success();
}

ui::UiActionResult TeamChatActionSink::sendMessage(
    const ui::chat::SendMessageView& message)
{
    if (!isTeamConversation(message.conversation))
    {
        return message.conversation.isValid()
                   ? ui::UiActionResult::fail(ui::UiActionFailure::Unsupported)
                   : ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
    }
    if (message.text == nullptr || message.text_len == 0)
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
    }

    ::team::ui::TeamUiSnapshot snap;
    if (!team_store_.load(snap) || !snap.has_team_id)
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::NotReady);
    }
    if (command_port_ == nullptr || !snap.has_team_psk)
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::NotReady);
    }
    if (!command_port_->setKeysFromPsk(snap.team_id,
                                       snap.security_round,
                                       snap.team_psk.data(),
                                       snap.team_psk.size()))
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::NotReady);
    }

    const uint32_t ts = currentTimestamp();
    const std::string text(message.text, message.text_len);

    team::proto::TeamChatMessage team_message;
    team_message.header.type = team::proto::TeamChatType::Text;
    team_message.header.ts = ts;
    team_message.header.msg_id = next_message_id_++;
    if (next_message_id_ == 0)
    {
        next_message_id_ = kTeamComposeMsgIdStart;
    }
    team_message.payload.assign(text.begin(), text.end());

    const bool sent = command_port_->sendTeamChat(team_message,
                                                  team_channel_raw_);
    if (!sent)
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::Rejected);
    }

    (void)team::ui::team_ui_chatlog_append_structured(snap.team_id,
                                                      0,
                                                      false,
                                                      ts,
                                                      team::proto::TeamChatType::Text,
                                                      team_message.payload);
    return ui::UiActionResult::success();
}

ui::UiActionResult TeamChatActionSink::markRead(ui::chat::ConversationId id)
{
    if (!id.isValid())
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
    }
    if (!isTeamConversation(id))
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::Unsupported);
    }

    ::team::ui::TeamUiSnapshot snap;
    if (!team_store_.load(snap) || !snap.has_team_id)
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::NotReady);
    }

    snap.team_chat_unread = 0;
    team_store_.save(snap);
    return ui::UiActionResult::success();
}

} // namespace ui::presentation_sources

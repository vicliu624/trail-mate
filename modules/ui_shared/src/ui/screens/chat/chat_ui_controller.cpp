/**
 * @file ui_controller.cpp
 * @brief Chat UI controller implementation
 */

#include "ui/screens/chat/chat_ui_controller.h"
#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/meshtastic/mt_radio_config.h"
#include "chat/usecase/contact_service.h"
#include "chat_presentation_adapters/chat_conversation_mapper.h"
#include "platform/ui/screen_runtime.h"
#include "sys/event_bus.h"
#include "team/protocol/team_location_marker.h"
#include "ui/app_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/screens/chat/chat_protocol_support.h"
#include "ui/team_actions/team_action_sink.h"
#include "ui/ui_common.h"
#include "ui/widgets/ime/ime_widget.h"
#include "ui/widgets/system_notification.h"
#include "ui_lvgl_ux_packs/common/key_verification_modal_renderer.h"
#include "ui_lvgl_ux_packs/common/team_position_picker_renderer.h"
#include "ui_presentation/key_verification/key_verification_model.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#ifndef CHAT_UI_LOG_ENABLE
#define CHAT_UI_LOG_ENABLE 0
#endif

#if CHAT_UI_LOG_ENABLE
#define CHAT_UI_LOG(...) std::printf(__VA_ARGS__)
#else
#define CHAT_UI_LOG(...)
#endif

namespace chat
{
namespace ui
{

namespace
{
namespace chat_support = chat::ui::support;

constexpr uint8_t kTeamChatChannelRaw = 2;
constexpr chat::ChannelId kTeamChatChannel =
    static_cast<chat::ChannelId>(kTeamChatChannelRaw);

const char* protocol_short_label(chat::MeshProtocol protocol)
{
    return chat::infra::meshProtocolShortName(protocol);
}

const char* channel_display_name(chat::MeshProtocol protocol, chat::ChannelId channel)
{
    if (protocol == chat::MeshProtocol::Meshtastic)
    {
        return chat::meshtastic::channelName(app::configFacade().getConfig().meshtastic_config,
                                             channel);
    }
    switch (channel)
    {
    case chat::ChannelId::SECONDARY:
        return "Secondary";
    case chat::ChannelId::PRIMARY:
    default:
        return "Primary";
    }
}

std::string base_conversation_name(const chat::ConversationId& conv)
{
    if (conv.peer == 0)
    {
        return ::ui::i18n::tr("Broadcast");
    }

    std::string contact_name = app::messagingFacade().getContactService().getContactName(conv.peer);
    if (!contact_name.empty())
    {
        return contact_name;
    }

    char buf[16] = {};
    std::snprintf(buf, sizeof(buf), "%08lX", static_cast<unsigned long>(conv.peer));
    return buf;
}

chat::ConversationId teamConversationId()
{
    return chat::ConversationId(kTeamChatChannel, 0, chat_support::active_mesh_protocol());
}

bool isTeamConversationId(const chat::ConversationId& conv)
{
    return conv.channel == kTeamChatChannel && conv.peer == 0;
}

chat::ConversationMeta legacyConversationMetaFromRow(
    const ::ui::chat::ConversationRow& row)
{
    chat::ConversationMeta meta;
    if (!chat_presentation_adapters::toCoreConversationId(row.id, meta.id))
    {
        return meta;
    }
    meta.name = row.title.c_str();
    meta.preview = row.subtitle.c_str();
    meta.unread = static_cast<int>(row.unread_count);
    meta.last_timestamp = 0;
    return meta;
}

void appendSnapshotConversationsToLegacy(const ::ui::chat::ChatWorkspaceSnapshot& snapshot,
                                         std::vector<chat::ConversationMeta>& out)
{
    for (size_t i = 0; i < snapshot.conversation_count; ++i)
    {
        chat::ConversationId core_id;
        if (!chat_presentation_adapters::toCoreConversationId(snapshot.conversations[i].id, core_id))
        {
            continue;
        }
        chat::ConversationMeta meta = legacyConversationMetaFromRow(snapshot.conversations[i]);
        meta.id = core_id;
        out.push_back(meta);
    }
}

bool legacyTeamConversationMetaFromSnapshot(
    const ::ui::chat::ChatWorkspaceSnapshot& snapshot,
    chat::ConversationMeta& out)
{
    if (!snapshot.header.valid)
    {
        return false;
    }

    for (size_t i = 0; i < snapshot.conversation_count; ++i)
    {
        const auto& row = snapshot.conversations[i];
        if (row.id.kind != ::ui::chat::ConversationKind::Team)
        {
            continue;
        }

        out = chat::ConversationMeta{};
        out.id = teamConversationId();
        out.name = row.title.c_str();
        out.preview = row.subtitle.c_str();
        out.unread = static_cast<int>(row.unread_count);
        out.last_timestamp = 0;
        return true;
    }

    return false;
}

void applySnapshotMessagesToConversation(
    const ::ui::chat::ChatWorkspaceSnapshot& snapshot,
    ChatConversationScreen& conversation)
{
    conversation.clearMessages();
    for (size_t i = 0; i < snapshot.message_count; ++i)
    {
        conversation.addMessage(snapshot.messages[i]);
    }
    conversation.scrollToBottom();
}

bool buildSelectedTeamSnapshot(::ui::chat::ChatWorkspaceModel& model,
                               ::ui::chat::ChatWorkspaceSnapshot& out)
{
    if (!model.buildSnapshot(out) || out.conversation_count == 0)
    {
        return false;
    }

    (void)model.selectConversation(out.conversations[0].id);
    (void)model.buildSnapshot(out);
    return out.header.valid && out.conversation_count > 0;
}

void showTeamSendFailure(::ui::UiActionResult result)
{
    const char* message = "Team chat send failed";
    if (result.failure == ::ui::UiActionFailure::NotReady)
    {
        message = "Team keys not ready";
    }
    else if (result.failure == ::ui::UiActionFailure::Unsupported)
    {
        message = "Team chat unsupported";
    }
    else if (result.failure == ::ui::UiActionFailure::InvalidInput)
    {
        message = "Message unavailable";
    }
    ::ui::SystemNotification::show(message, 2000);
}

void showTeamLocationSendFailure(::ui::UiActionResult result)
{
    const char* message = "Team location send failed";
    if (result.failure == ::ui::UiActionFailure::NotReady)
    {
        message = "Team location not ready";
    }
    else if (result.failure == ::ui::UiActionFailure::Unsupported)
    {
        message = "Team location unsupported";
    }
    else if (result.failure == ::ui::UiActionFailure::InvalidInput)
    {
        message = "Invalid marker";
    }
    ::ui::SystemNotification::show(message, 2000);
}

const char* key_verification_action_failure_message(::ui::UiActionResult result)
{
    if (result.failure == ::ui::UiActionFailure::NotReady)
    {
        return "Key verification unavailable";
    }
    if (result.failure == ::ui::UiActionFailure::Unsupported)
    {
        return "Key verification unsupported";
    }
    if (result.failure == ::ui::UiActionFailure::InvalidInput)
    {
        return "Key verification unavailable";
    }
    return "Key verification failed";
}

void handle_message_list_action(chat::ui::ChatMessageListScreen::ActionIntent intent,
                                const chat::ConversationId& conv,
                                void* user_data)
{
    auto* controller = static_cast<UiController*>(user_data);
    if (!controller)
    {
        return;
    }
    if (intent == chat::ui::ChatMessageListScreen::ActionIntent::SelectConversation)
    {
        controller->onChannelClicked(conv);
        return;
    }
    if (intent == chat::ui::ChatMessageListScreen::ActionIntent::Back)
    {
        controller->exitToMenu();
    }
}

void handle_conversation_action(chat::ui::ChatConversationScreen::ActionIntent intent, void* user_data)
{
    auto* controller = static_cast<UiController*>(user_data);
    if (controller)
    {
        controller->handleConversationAction(intent);
    }
}

void handle_compose_back(void* user_data)
{
    auto* controller = static_cast<UiController*>(user_data);
    if (controller)
    {
        controller->handleComposeAction(chat::ui::ChatComposeScreen::ActionIntent::Cancel);
    }
}

void handle_compose_action(chat::ui::ChatComposeScreen::ActionIntent intent, void* user_data)
{
    auto* controller = static_cast<UiController*>(user_data);
    if (controller)
    {
        controller->handleComposeAction(intent);
    }
}

void handle_conversation_back(void* user_data)
{
    auto* controller = static_cast<UiController*>(user_data);
    if (controller)
    {
        controller->backToList();
    }
}
} // namespace

UiController::UiController(lv_obj_t* parent,
                           chat::ChatService& service,
                           ::ui::chat::ChatWorkspaceModel& chat_model,
                           ::ui::chat::ChatWorkspaceModel& team_chat_model,
                           ::ui::team_actions::ITeamActionSink* team_action_sink,
                           ::ui::key_verification::KeyVerificationModel*
                               key_verification_model,
                           chat::ChannelId initial_channel,
                           ExitRequestCallback exit_request,
                           void* exit_request_user_data)
    : parent_(parent), service_(service), chat_model_(chat_model),
      team_chat_model_(team_chat_model),
      team_action_sink_(team_action_sink),
      key_verification_model_(key_verification_model),
      state_(State::ChannelList),
      current_channel_(initial_channel),
      current_conv_(chat::ConversationId(initial_channel, 0, chat_support::active_mesh_protocol())),
      exit_request_(exit_request), exit_request_user_data_(exit_request_user_data)
{
}

UiController::~UiController()
{
    closeTeamPositionPicker(false);
    team_position_picker_.reset();
    closeKeyVerificationModal(false);
    stopTeamConversationTimer();
    service_.setModelEnabled(false);
    channel_list_.reset();
    conversation_.reset();
    cleanupComposeIme();
    compose_.reset();
}

void UiController::cleanupComposeIme()
{
    if (compose_ime_)
    {
        compose_ime_->detach();
        compose_ime_.reset();
    }
}

void UiController::init()
{
    TeamPositionPickerRenderer::Callbacks callbacks;
    callbacks.on_icon_selected = [](void* user_data, uint8_t icon_id)
    {
        auto* controller = static_cast<UiController*>(user_data);
        if (controller)
        {
            controller->onTeamPositionIconSelected(icon_id);
        }
    };
    callbacks.on_cancel = [](void* user_data)
    {
        auto* controller = static_cast<UiController*>(user_data);
        if (controller)
        {
            controller->onTeamPositionCancel();
        }
    };
    callbacks.user_data = this;
    team_position_picker_.reset(
        new TeamPositionPickerRenderer(parent_, callbacks));
    switchToChannelList();
}

void UiController::update()
{
    // Refresh UI only when an event marks the conversation list dirty.
    refreshUnreadCounts(false);
}

void UiController::onChannelClicked(chat::ConversationId conv)
{
    if (channel_list_)
    {
        handleChannelSelected(conv);
    }
}

void UiController::backToList()
{
    switchToChannelList();
}

void UiController::onInput(const sys::InputEvent& event)
{
    switch (state_)
    {
    case State::ChannelList:
        if (event.input_type == sys::InputEvent::RotaryTurn)
        {
            // Handle rotary navigation
            // (Implementation depends on rotary event details)
        }
        else if (event.input_type == sys::InputEvent::RotaryPress)
        {
            if (channel_list_)
            {
                chat::ConversationId selected_conv{};
                if (channel_list_->tryGetSelectedConversation(&selected_conv))
                {
                    handleChannelSelected(selected_conv);
                }
            }
        }
        else if (event.input_type == sys::InputEvent::KeyPress && event.value == 27)
        {
            // ESC - return to main menu (handled by parent)
        }
        break;

    case State::Conversation:
        if (event.input_type == sys::InputEvent::KeyPress && event.value == 27)
        {
            // ESC - return to channel list
            switchToChannelList();
        }
        break;

    case State::Compose:
        if (event.input_type == sys::InputEvent::KeyPress && event.value == 27)
        {
            // ESC - cancel compose
            switchToConversation(current_conv_);
        }
        break;

    default:
        break;
    }
}

void UiController::onRuntimeMessageArrived(chat::MessageId msg_id)
{
    CHAT_UI_LOG("[UiController::onRuntimeMessageArrived] msg_id=%lu, state=%d, current_channel=%d\n",
                static_cast<unsigned long>(msg_id), (int)state_, (int)current_channel_);

    const ChatMessage* latest = service_.getMessage(msg_id);
    if (latest)
    {
        const bool is_current_conversation =
            (state_ == State::Conversation) &&
            (current_conv_ == chat::ConversationId(latest->channel,
                                                   latest->peer,
                                                   latest->protocol));
        updateConversationMetaForMessage(*latest, !is_current_conversation);
        if (is_current_conversation)
        {
            (void)updateConversationViewForIncoming(*latest);
            reloadConversationView();
            (void)chat_model_.markRead(
                chat_presentation_adapters::toUiConversationId(current_conv_));
        }
        else
        {
            conversation_list_dirty_ = true;
        }
    }
    refreshUnreadCounts(false);
}

void UiController::onRuntimeSendResult(chat::MessageId msg_id)
{
    if (state_ == State::Conversation && conversation_)
    {
        const ChatMessage* msg = service_.getMessage(msg_id);
        if (!msg || !conversation_->updateMessageStatus(msg_id, msg->status))
        {
            reloadConversationView();
        }
    }
}

void UiController::onRuntimeUnreadChanged()
{
    conversation_list_dirty_ = true;
    refreshUnreadCounts(false);
}

void UiController::showKeyVerification(
    const ::ui::key_verification::KeyVerificationSnapshot& snapshot)
{
    renderKeyVerificationModal(snapshot);
}

void UiController::switchToChannelList()
{
    closeTeamPositionPicker(true);
    state_ = State::ChannelList;
    stopTeamConversationTimer();
    team_conv_active_ = false;
    CHAT_UI_LOG("[UiController] switchToChannelList: parent=%p active=%p sleeping=%d\n",
                parent_, lv_screen_active(), platform::ui::screen::is_sleeping() ? 1 : 0);
    if (lv_obj_t* active = lv_screen_active())
    {
        CHAT_UI_LOG("[UiController] switchToChannelList active child count=%u\n",
                    (unsigned)lv_obj_get_child_cnt(active));
    }
    if (parent_)
    {
        CHAT_UI_LOG("[UiController] switchToChannelList parent child count=%u\n",
                    (unsigned)lv_obj_get_child_cnt(parent_));
    }

    if (conversation_)
    {
        conversation_.reset();
    }
    if (compose_)
    {
        cleanupComposeIme();
        compose_.reset();
    }

    if (!channel_list_)
    {
        channel_list_.reset(new ChatMessageListScreen(parent_));
        channel_list_->setActionCallback(handle_message_list_action, this);
    }

    service_.setModelEnabled(true);
    refreshUnreadCounts(true);
}

void UiController::switchToConversation(chat::ConversationId conv)
{
    closeTeamPositionPicker(true);
    state_ = State::Conversation;
    current_channel_ = conv.channel;
    current_conv_ = conv;
    team_conv_active_ = isTeamConversation(conv);
    stopTeamConversationTimer();
    CHAT_UI_LOG("[UiController] switchToConversation: parent=%p active=%p sleeping=%d conv_peer=%08lX\n",
                parent_, lv_screen_active(), platform::ui::screen::is_sleeping() ? 1 : 0,
                (unsigned long)conv.peer);
    if (lv_obj_t* active = lv_screen_active())
    {
        CHAT_UI_LOG("[UiController] switchToConversation active child count=%u\n",
                    (unsigned)lv_obj_get_child_cnt(active));
    }
    if (parent_)
    {
        CHAT_UI_LOG("[UiController] switchToConversation parent child count=%u\n",
                    (unsigned)lv_obj_get_child_cnt(parent_));
    }

    if (channel_list_)
    {
        channel_list_.reset();
    }
    if (compose_)
    {
        cleanupComposeIme();
        compose_.reset();
    }

#if defined(ARDUINO_T_WATCH_S3)
    if (!team_conv_active_ && conv.peer == 0 && conv.channel == chat::ChannelId::PRIMARY)
    {
        auto recent = service_.getRecentMessages(conv, 1);
        if (recent.empty())
        {
            switchToCompose(conv);
            return;
        }
    }
#endif

    if (!conversation_)
    {
        conversation_.reset(new ChatConversationScreen(parent_, conv));
        conversation_->setActionCallback(handle_conversation_action, this);
        conversation_->setBackCallback(handle_conversation_back, this);
    }
    const bool can_reply = team_conv_active_
                               ? chat_support::supports_team_chat()
                               : (conv.protocol == chat_support::active_mesh_protocol() &&
                                  chat_support::supports_local_text_chat());
    conversation_->setReplyEnabled(can_reply);

    if (team_conv_active_)
    {
        const bool loaded =
            buildSelectedTeamSnapshot(team_chat_model_, team_chat_snapshot_buffer_);
        std::string title = loaded && team_chat_snapshot_buffer_.conversation_count > 0
                                ? team_chat_snapshot_buffer_.conversations[0].title.c_str()
                                : "Team";
        const uint16_t unread = loaded && team_chat_snapshot_buffer_.conversation_count > 0
                                    ? team_chat_snapshot_buffer_.conversations[0].unread_count
                                    : 0;
        conversation_->setHeaderText(title.c_str(), nullptr);
        conversation_->updateBatteryFromBoard();
        if (loaded)
        {
            applySnapshotMessagesToConversation(team_chat_snapshot_buffer_, *conversation_);
        }
        startTeamConversationTimer();
        if (loaded && unread != 0)
        {
            (void)team_chat_model_.markRead(
                team_chat_snapshot_buffer_.conversations[0].id);
            sys::EventBus::publish(
                new sys::ChatUnreadChangedEvent(kTeamChatChannelRaw, 0), 0);
        }
        return;
    }

    const ::ui::chat::ConversationId ui_conv =
        chat_presentation_adapters::toUiConversationId(conv);
    (void)chat_model_.selectConversation(ui_conv);

    // Update header (prefer contact name, else short_name)
    std::string title = resolveConversationDisplayName(conv);
    if (conv.peer != 0)
    {
        std::string contact_name = app::messagingFacade().getContactService().getContactName(conv.peer);
        if (!contact_name.empty())
        {
            title = contact_name;
        }
        else
        {
            size_t total = 0;
            auto convs = service_.getConversations(0, 0, &total);
            for (const auto& c : convs)
            {
                if (c.id == conv)
                {
                    title = c.name;
                    break;
                }
            }
        }
    }
    conversation_->updateBatteryFromBoard();
    std::string header = "[" + std::string(protocol_short_label(conv.protocol)) + "] " + title;
    conversation_->setHeaderText(header.c_str(), nullptr);

    if (loadChatSnapshot())
    {
        applySnapshotMessagesToConversation(chat_snapshot_buffer_, *conversation_);
    }
    (void)chat_model_.markRead(ui_conv);
}

void UiController::switchToCompose(chat::ConversationId conv)
{
    closeTeamPositionPicker(true);
    const bool is_team_conv = isTeamConversation(conv);
    if (!is_team_conv && conv.protocol != chat_support::active_mesh_protocol())
    {
        ::ui::SystemNotification::show("Conversation protocol mismatch", 2000);
        return;
    }
    if (!is_team_conv && !chat_support::supports_local_text_chat())
    {
        ::ui::SystemNotification::show(chat_support::local_text_chat_unavailable_message(), 2200);
        return;
    }
    if (is_team_conv && !chat_support::supports_team_chat())
    {
        ::ui::SystemNotification::show(chat_support::team_chat_unavailable_message(), 2200);
        return;
    }

    state_ = State::Compose;
    current_channel_ = conv.channel;
    current_conv_ = conv;
    team_conv_active_ = is_team_conv;

    if (!is_team_conv)
    {
        (void)chat_model_.selectConversation(
            chat_presentation_adapters::toUiConversationId(conv));
    }

    stopTeamConversationTimer();
    CHAT_UI_LOG("[UiController] switchToCompose: parent=%p active=%p sleeping=%d conv_peer=%08lX\n",
                parent_, lv_screen_active(), platform::ui::screen::is_sleeping() ? 1 : 0,
                (unsigned long)conv.peer);
    if (lv_obj_t* active = lv_screen_active())
    {
        CHAT_UI_LOG("[UiController] switchToCompose active child count=%u\n",
                    (unsigned)lv_obj_get_child_cnt(active));
    }
    if (parent_)
    {
        CHAT_UI_LOG("[UiController] switchToCompose parent child count=%u\n",
                    (unsigned)lv_obj_get_child_cnt(parent_));
    }

    if (channel_list_)
    {
        channel_list_.reset();
    }
    if (conversation_)
    {
        conversation_.reset();
    }

    if (!compose_)
    {
        compose_.reset(new ChatComposeScreen(parent_, conv));
        compose_->setActionCallback(handle_compose_action, this);
        compose_->setBackCallback(handle_compose_back, this);
    }

    lv_obj_t* compose_content = compose_->getContent();
    lv_obj_t* compose_textarea = compose_->getTextarea();
    if (compose_content && compose_textarea)
    {
        if (compose_ime_)
        {
            compose_ime_->detach();
        }
        else
        {
            compose_ime_.reset(new ::ui::widgets::ImeWidget());
        }
        compose_ime_->init(compose_content, compose_textarea);
        compose_->attachImeWidget(compose_ime_.get());
        if (lv_group_t* g = lv_group_get_default())
        {
            lv_group_add_obj(g, compose_ime_->focus_obj());
        }
    }

    std::string title = resolveConversationDisplayName(conv);
    if (team_conv_active_)
    {
        if (buildSelectedTeamSnapshot(team_chat_model_, team_chat_snapshot_buffer_) &&
            team_chat_snapshot_buffer_.conversation_count > 0)
        {
            title = team_chat_snapshot_buffer_.conversations[0].title.c_str();
        }
        else
        {
            title = "Team";
        }
        compose_->setHeaderText(title.c_str(), nullptr);
        compose_->setActionLabels("Send", "Cancel");
        compose_->setPositionButton("Position", true);
        return;
    }

    if (conv.peer != 0)
    {
        std::string contact_name = app::messagingFacade().getContactService().getContactName(conv.peer);
        if (!contact_name.empty())
        {
            title = contact_name;
        }
        else
        {
            size_t total = 0;
            auto convs = service_.getConversations(0, 0, &total);
            for (const auto& c : convs)
            {
                if (c.id == conv)
                {
                    title = c.name;
                    break;
                }
            }
        }
    }
    std::string header = "[" + std::string(protocol_short_label(conv.protocol)) + "] " + title;
    compose_->setHeaderText(header.c_str(), nullptr);
    compose_->setPositionButton(nullptr, false);
}

void UiController::handleChannelSelected(const chat::ConversationId& conv)
{
    switchToConversation(conv);
}

void UiController::handleSendMessage(const std::string& text)
{
    if (text.empty())
    {
        return;
    }
    if (team_conv_active_)
    {
        const ::ui::UiActionResult result =
            team_chat_model_.sendMessage(text.c_str());
        if (!result.ok)
        {
            showTeamSendFailure(result);
        }
        handleComposeSendDone(result.ok, false);
        return;
    }
    if (!chat_support::supports_local_text_chat())
    {
        ::ui::SystemNotification::show(chat_support::local_text_chat_unavailable_message(), 2200);
        return;
    }
    const ::ui::UiActionResult result = chat_model_.sendMessage(text.c_str());
    if (!result.ok)
    {
        const char* message = "Send failed";
        if (result.failure == ::ui::UiActionFailure::Unsupported)
        {
            message = "Conversation unsupported";
        }
        else if (result.failure == ::ui::UiActionFailure::InvalidInput)
        {
            message = "Message unavailable";
        }
        ::ui::SystemNotification::show(message, 2000);
    }
    handleComposeSendDone(result.ok, false);
}

void UiController::handleComposeSendDone(bool ok, bool timeout)
{
    (void)ok;
    (void)timeout;
    if (state_ == State::Compose)
    {
        switchToConversation(current_conv_);
    }
}

void UiController::handleComposeSendDoneCallback(bool ok, bool timeout, void* user_data)
{
    auto* controller = static_cast<UiController*>(user_data);
    if (controller)
    {
        controller->handleComposeSendDone(ok, timeout);
    }
}

void UiController::refreshUnreadCounts()
{
    refreshUnreadCounts(true);
}

void UiController::refreshUnreadCounts(const bool force_reload)
{
    if (!channel_list_)
    {
        return;
    }

    if (force_reload || conversation_list_dirty_ || cached_conversations_.empty())
    {
        syncConversationListFromStore();
    }
    applyConversationListToUi();
}

void UiController::syncConversationListFromStore()
{
    cached_conversations_.clear();
    if (loadChatSnapshot())
    {
        appendSnapshotConversationsToLegacy(chat_snapshot_buffer_,
                                            cached_conversations_);
    }
    normalizeConversationNames(cached_conversations_);

    chat::ConversationMeta team_conv;
    if (loadTeamChatSnapshot() &&
        legacyTeamConversationMetaFromSnapshot(team_chat_snapshot_buffer_, team_conv))
    {
        cached_conversations_.insert(cached_conversations_.begin(), team_conv);
    }

    conversation_list_dirty_ = false;
}

void UiController::normalizeConversationNames(std::vector<chat::ConversationMeta>& convs) const
{
    for (auto& conv : convs)
    {
        if (isTeamConversationId(conv.id))
        {
            continue;
        }

        conv.name = base_conversation_name(conv.id);
    }

    for (auto& conv : convs)
    {
        if (isTeamConversationId(conv.id))
        {
            continue;
        }

        int channel_variant_count = 0;
        for (const auto& other : convs)
        {
            if (isTeamConversationId(other.id))
            {
                continue;
            }
            if (other.id.protocol != conv.id.protocol)
            {
                continue;
            }
            if (other.id.peer != conv.id.peer)
            {
                continue;
            }
            channel_variant_count++;
        }

        if (channel_variant_count > 1)
        {
            conv.name += " (";
            conv.name += channel_display_name(conv.id.protocol, conv.id.channel);
            conv.name += ")";
        }
    }
}

void UiController::applyConversationListToUi()
{
    if (!channel_list_)
    {
        return;
    }

    channel_list_->setConversations(cached_conversations_);
    channel_list_->updateBatteryFromBoard();
}

std::string UiController::resolveConversationDisplayName(const chat::ConversationId& conv) const
{
    if (isTeamConversation(conv))
    {
        return "Team";
    }

    for (const auto& item : cached_conversations_)
    {
        if (item.id == conv && !item.name.empty())
        {
            return item.name;
        }
    }

    return base_conversation_name(conv);
}

void UiController::updateConversationMetaForMessage(const chat::ChatMessage& msg,
                                                    const bool increment_unread)
{
    if (isTeamConversation(chat::ConversationId(msg.channel, msg.peer, msg.protocol)))
    {
        conversation_list_dirty_ = true;
        return;
    }

    chat::ConversationMeta meta;
    meta.id = chat::ConversationId(msg.channel, msg.peer, msg.protocol);
    meta.name = base_conversation_name(meta.id);
    meta.preview = msg.text;
    meta.last_timestamp = msg.timestamp;
    meta.unread = (increment_unread && msg.status == chat::MessageStatus::Incoming) ? 1 : 0;

    bool found = false;
    for (auto it = cached_conversations_.begin(); it != cached_conversations_.end(); ++it)
    {
        if (!(it->id == meta.id))
        {
            continue;
        }
        found = true;
        meta.unread += it->unread;
        if (!increment_unread && msg.status == chat::MessageStatus::Incoming)
        {
            meta.unread = 0;
        }
        cached_conversations_.erase(it);
        break;
    }

    cached_conversations_.insert(cached_conversations_.begin(), meta);
    normalizeConversationNames(cached_conversations_);
}

bool UiController::updateConversationViewForIncoming(const chat::ChatMessage& msg)
{
    if (!conversation_)
    {
        return false;
    }

    if (!(current_conv_ == chat::ConversationId(msg.channel, msg.peer, msg.protocol)))
    {
        return false;
    }

    reloadConversationView();
    return true;
}

void UiController::reloadConversationView()
{
    if (!conversation_ || team_conv_active_)
    {
        return;
    }

    if (!loadChatSnapshot())
    {
        return;
    }
    applySnapshotMessagesToConversation(chat_snapshot_buffer_, *conversation_);
}

bool UiController::isTeamConversation(const chat::ConversationId& conv) const
{
    return isTeamConversationId(conv);
}

bool UiController::loadChatSnapshot()
{
    return chat_model_.buildSnapshot(chat_snapshot_buffer_);
}

bool UiController::loadTeamChatSnapshot()
{
    return team_chat_model_.buildSnapshot(team_chat_snapshot_buffer_);
}

void UiController::refreshTeamConversation()
{
    if (!conversation_ || !team_conv_active_)
    {
        return;
    }

    if (loadTeamChatSnapshot())
    {
        applySnapshotMessagesToConversation(team_chat_snapshot_buffer_, *conversation_);
    }
}

void UiController::startTeamConversationTimer()
{
    if (team_conv_timer_)
    {
        lv_timer_resume(team_conv_timer_);
        return;
    }
    team_conv_timer_ = lv_timer_create([](lv_timer_t* timer)
                                       {
        auto* controller = static_cast<UiController*>(lv_timer_get_user_data(timer));
        if (controller)
        {
            controller->refreshTeamConversation();
        } },
                                       1000, this);
    if (team_conv_timer_)
    {
        lv_timer_set_repeat_count(team_conv_timer_, -1);
    }
}

void UiController::stopTeamConversationTimer()
{
    if (!team_conv_timer_)
    {
        return;
    }
    lv_timer_del(team_conv_timer_);
    team_conv_timer_ = nullptr;
}

bool UiController::isTeamPositionPickerOpen() const
{
    return team_position_picker_ && team_position_picker_->isOpen();
}

void UiController::updateTeamPositionPickerHint(uint8_t icon_id)
{
    if (team_position_picker_)
    {
        team_position_picker_->updateHint(icon_id);
    }
}

void UiController::openTeamPositionPicker()
{
    if (!team_conv_active_ || !compose_ || !parent_)
    {
        return;
    }
    if (!team_position_picker_)
    {
        return;
    }
    (void)team_position_picker_->open();
}

void UiController::closeTeamPositionPicker(bool restore_group)
{
    if (team_position_picker_)
    {
        team_position_picker_->close(restore_group);
    }
}

bool UiController::isKeyVerificationModalOpen() const
{
    return key_verify_modal_.overlay != nullptr;
}

void UiController::clearKeyVerificationError()
{
    chat::ui::clearKeyVerificationError(key_verify_modal_);
}

void UiController::key_verify_submit_event_cb(lv_event_t* e)
{
    auto* controller = static_cast<UiController*>(lv_event_get_user_data(e));
    if (!controller)
    {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_KEY)
    {
        lv_key_t key = static_cast<lv_key_t>(lv_event_get_key(e));
        if (key != LV_KEY_ENTER)
        {
            return;
        }
    }
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_KEY)
    {
        controller->submitKeyVerificationInput();
    }
}

void UiController::key_verify_close_event_cb(lv_event_t* e)
{
    auto* controller = static_cast<UiController*>(lv_event_get_user_data(e));
    if (!controller)
    {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_KEY)
    {
        lv_key_t key = static_cast<lv_key_t>(lv_event_get_key(e));
        if (key != LV_KEY_ENTER)
        {
            return;
        }
    }
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_KEY)
    {
        controller->closeKeyVerificationModal(true);
    }
}

void UiController::key_verify_trust_event_cb(lv_event_t* e)
{
    auto* controller = static_cast<UiController*>(lv_event_get_user_data(e));
    if (!controller)
    {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_KEY)
    {
        lv_key_t key = static_cast<lv_key_t>(lv_event_get_key(e));
        if (key != LV_KEY_ENTER)
        {
            return;
        }
    }
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_KEY)
    {
        controller->trustKeyFromVerificationModal();
    }
}

void UiController::destroyKeyVerificationModal(bool restore_group)
{
    chat::ui::destroyKeyVerificationModal(
        key_verify_modal_,
        key_verify_ime_,
        restore_group);
}

void UiController::renderKeyVerificationModal(
    const ::ui::key_verification::KeyVerificationSnapshot& snapshot)
{
    KeyVerificationModalCallbacks callbacks;
    callbacks.submit = key_verify_submit_event_cb;
    callbacks.close = key_verify_close_event_cb;
    callbacks.trust = key_verify_trust_event_cb;
    callbacks.user_data = this;
    chat::ui::renderKeyVerificationModal(
        parent_,
        snapshot,
        callbacks,
        key_verify_modal_,
        key_verify_ime_);
}

void UiController::closeKeyVerificationModal(bool restore_group)
{
    destroyKeyVerificationModal(restore_group);
    if (key_verification_model_ != nullptr)
    {
        key_verification_model_->clearSelection();
    }
}

void UiController::submitKeyVerificationInput()
{
    if (!key_verify_modal_.textarea || key_verification_model_ == nullptr)
    {
        return;
    }

    clearKeyVerificationError();
    const char* text = lv_textarea_get_text(key_verify_modal_.textarea);
    if (!text || text[0] == '\0')
    {
        if (key_verify_modal_.error_label)
        {
            ::ui::i18n::set_label_text(
                key_verify_modal_.error_label,
                "Enter the 6-digit number");
        }
        return;
    }

    char* end_ptr = nullptr;
    unsigned long parsed = std::strtoul(text, &end_ptr, 10);
    if (!end_ptr || *end_ptr != '\0' || parsed > 999999UL)
    {
        if (key_verify_modal_.error_label)
        {
            ::ui::i18n::set_label_text(
                key_verify_modal_.error_label,
                "Invalid number");
        }
        return;
    }

    const auto result = key_verification_model_->submitNumber(
        static_cast<uint32_t>(parsed));
    if (!result.ok)
    {
        if (key_verify_modal_.error_label)
        {
            ::ui::i18n::set_label_text_raw(
                key_verify_modal_.error_label,
                key_verification_action_failure_message(result));
        }
        return;
    }

    ::ui::SystemNotification::show("Verification number sent", 2000);
    closeKeyVerificationModal(true);
}

void UiController::trustKeyFromVerificationModal()
{
    if (key_verification_model_ == nullptr)
    {
        closeKeyVerificationModal(true);
        return;
    }

    const auto result = key_verification_model_->accept();
    ::ui::SystemNotification::show(
        result.ok ? "Key marked trusted"
                  : key_verification_action_failure_message(result),
        2000);
    closeKeyVerificationModal(true);
}

void UiController::onTeamPositionCancel()
{
    closeTeamPositionPicker(true);
}

bool UiController::sendTeamLocationWithIcon(uint8_t icon_id)
{
    if (!team::proto::team_location_marker_icon_is_valid(icon_id))
    {
        ::ui::SystemNotification::show("Invalid marker", 1500);
        return false;
    }

    if (team_action_sink_ == nullptr)
    {
        ::ui::SystemNotification::show("Team chat send failed", 2000);
        return false;
    }

    ::ui::team_actions::TeamActionRequest request;
    request.kind = ::ui::team_actions::TeamActionKind::LocationMarker;
    request.location.use_current_location = true;
    request.location.marker_icon = icon_id;
    request.location.label = team::proto::team_location_marker_icon_name(icon_id);

    const auto result = team_action_sink_->sendTeamAction(request);
    if (!result.ok)
    {
        showTeamLocationSendFailure(result);
    }
    return result.ok;
}

void UiController::onTeamPositionIconSelected(uint8_t icon_id)
{
    closeTeamPositionPicker(true);
    sendTeamLocationWithIcon(icon_id);
    switchToConversation(current_conv_);
}

void UiController::handleConversationAction(ChatConversationScreen::ActionIntent intent)
{
    if (intent == ChatConversationScreen::ActionIntent::Reply)
    {
        if (!team_conv_active_ && current_conv_.protocol != chat_support::active_mesh_protocol())
        {
            ::ui::SystemNotification::show("Reply disabled for this protocol", 2000);
            return;
        }
        if (!team_conv_active_ && !chat_support::supports_local_text_chat())
        {
            ::ui::SystemNotification::show(chat_support::local_text_chat_unavailable_message(), 2200);
            return;
        }
        if (team_conv_active_ && !chat_support::supports_team_chat())
        {
            ::ui::SystemNotification::show(chat_support::team_chat_unavailable_message(), 2200);
            return;
        }
        switchToCompose(current_conv_);
    }
}

void UiController::handleComposeAction(ChatComposeScreen::ActionIntent intent)
{
    if (!compose_)
    {
        return;
    }
    if (isTeamPositionPickerOpen())
    {
        if (intent == ChatComposeScreen::ActionIntent::Cancel)
        {
            onTeamPositionCancel();
        }
        return;
    }
    if (intent == ChatComposeScreen::ActionIntent::Cancel)
    {
        switchToConversation(current_conv_);
        return;
    }

    if (team_conv_active_)
    {
        if (intent == ChatComposeScreen::ActionIntent::Position)
        {
            openTeamPositionPicker();
            return;
        }

        std::string text = compose_->getText();
        if (text.empty())
        {
            switchToConversation(current_conv_);
            return;
        }
        const ::ui::UiActionResult result =
            team_chat_model_.sendMessage(text.c_str());
        if (!result.ok)
        {
            showTeamSendFailure(result);
        }
        switchToConversation(current_conv_);
        return;
    }

    if (intent == ChatComposeScreen::ActionIntent::Send)
    {
        std::string text = compose_->getText();
        if (!text.empty())
        {
            handleSendMessage(text);
            return;
        }
    }
    switchToConversation(current_conv_);
}

void UiController::exitToMenu()
{
    if (exiting_)
    {
        return;
    }
    closeTeamPositionPicker(false);
    exiting_ = true;
    stopTeamConversationTimer();
    team_conv_active_ = false;
    if (exit_request_)
    {
        exit_request_(exit_request_user_data_);
    }
    else
    {
        ui_request_exit_to_menu();
    }
}

} // namespace ui
} // namespace chat

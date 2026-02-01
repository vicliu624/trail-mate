/**
 * @file ui_controller.cpp
 * @brief Chat UI controller implementation
 */

#include "ui_controller.h"
#include <Arduino.h>
#include "../../app/app_context.h"
#include "screens/team/team_ui_store.h"
#include "widgets/system_notification.h"
#include "../sys/event_bus.h"
#include "ui_common.h"
#include "../gps/gps_service_api.h"
#include <cmath>
#include <ctime>

extern bool isScreenSleeping();

namespace chat
{
namespace ui
{

namespace
{
constexpr uint8_t kTeamChatChannelRaw = 2;
constexpr chat::ChannelId kTeamChatChannel =
    static_cast<chat::ChannelId>(kTeamChatChannelRaw);
constexpr uint32_t kTeamComposeMsgIdStart = 1;
uint32_t s_team_msg_id = kTeamComposeMsgIdStart;

chat::ConversationId teamConversationId()
{
    return chat::ConversationId(kTeamChatChannel, 0);
}

bool isTeamConversationId(const chat::ConversationId& conv)
{
    return conv.channel == kTeamChatChannel && conv.peer == 0;
}

const char* team_command_name(team::proto::TeamCommandType type)
{
    switch (type) {
    case team::proto::TeamCommandType::RallyTo:
        return "RallyTo";
    case team::proto::TeamCommandType::MoveTo:
        return "MoveTo";
    case team::proto::TeamCommandType::Hold:
        return "Hold";
    default:
        return "Command";
    }
}

std::string truncate_text(const std::string& text, size_t max_len)
{
    if (text.size() <= max_len) {
        return text;
    }
    if (max_len <= 3) {
        return text.substr(0, max_len);
    }
    return text.substr(0, max_len - 3) + "...";
}

std::string format_team_chat_entry(const team::ui::TeamChatLogEntry& entry)
{
    if (entry.type == team::proto::TeamChatType::Text) {
        std::string text(entry.payload.begin(), entry.payload.end());
        return truncate_text(text, 160);
    }
    if (entry.type == team::proto::TeamChatType::Location) {
        team::proto::TeamChatLocation loc;
        if (team::proto::decodeTeamChatLocation(entry.payload.data(),
                                                entry.payload.size(),
                                                &loc)) {
            double lat = static_cast<double>(loc.lat_e7) / 1e7;
            double lon = static_cast<double>(loc.lon_e7) / 1e7;
            char buf[128];
            if (!loc.label.empty()) {
                snprintf(buf, sizeof(buf), "Location: %s %.5f, %.5f",
                         loc.label.c_str(), lat, lon);
            } else {
                snprintf(buf, sizeof(buf), "Location: %.5f, %.5f", lat, lon);
            }
            return std::string(buf);
        }
        return "Location";
    }
    if (entry.type == team::proto::TeamChatType::Command) {
        team::proto::TeamChatCommand cmd;
        if (team::proto::decodeTeamChatCommand(entry.payload.data(),
                                               entry.payload.size(),
                                               &cmd)) {
            const char* name = team_command_name(cmd.cmd_type);
            double lat = static_cast<double>(cmd.lat_e7) / 1e7;
            double lon = static_cast<double>(cmd.lon_e7) / 1e7;
            char buf[160];
            if (cmd.lat_e7 != 0 || cmd.lon_e7 != 0) {
                if (!cmd.note.empty()) {
                    snprintf(buf, sizeof(buf), "Command: %s %.5f, %.5f %s",
                             name, lat, lon, cmd.note.c_str());
                } else {
                    snprintf(buf, sizeof(buf), "Command: %s %.5f, %.5f",
                             name, lat, lon);
                }
            } else if (!cmd.note.empty()) {
                snprintf(buf, sizeof(buf), "Command: %s %s", name, cmd.note.c_str());
            } else {
                snprintf(buf, sizeof(buf), "Command: %s", name);
            }
            return std::string(buf);
        }
        return "Command";
    }
    return "Message";
}

std::string team_title_from_snapshot(const team::ui::TeamUiSnapshot& snap)
{
    if (!snap.team_name.empty()) {
        return snap.team_name;
    }
    return "Team";
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

UiController::UiController(lv_obj_t* parent, chat::ChatService& service)
    : parent_(parent), service_(service), state_(State::ChannelList),
      current_channel_(chat::ChannelId::PRIMARY),
      current_conv_(chat::ConversationId(chat::ChannelId::PRIMARY, 0))
{
}

UiController::~UiController()
{
    stopTeamConversationTimer();
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
    switchToChannelList();
    refreshUnreadCounts();
}

void UiController::update()
{
    // Process incoming messages
    service_.processIncoming();

    // Refresh UI if needed
    if (state_ == State::ChannelList && channel_list_)
    {
        refreshUnreadCounts();
    }
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
                handleChannelSelected(channel_list_->getSelectedConversation());
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

void UiController::onChatEvent(sys::Event* event)
{
    if (!event)
    {
        return;
    }

    switch (event->type)
    {
    case sys::EventType::ChatNewMessage:
    {
        sys::ChatNewMessageEvent* msg_event = (sys::ChatNewMessageEvent*)event;
        Serial.printf("[UiController::onChatEvent] ChatNewMessage received: channel=%d, state=%d, current_channel=%d\n",
                      msg_event->channel, (int)state_, (int)current_channel_);

        // Note: Haptic feedback is now handled globally in AppContext::update()
        // No need to call vibrator() here

        if (state_ == State::Conversation &&
            (uint8_t)current_channel_ == msg_event->channel)
        {
            Serial.printf("[UiController::onChatEvent] Updating conversation UI...\n");
            auto messages = service_.getRecentMessages(current_conv_, 50);
            conversation_->clearMessages();
            for (const auto& m : messages)
            {
                conversation_->addMessage(m);
            }
            conversation_->scrollToBottom();
        }
        refreshUnreadCounts();
        break;
    }

    case sys::EventType::ChatSendResult:
    {
        sys::ChatSendResultEvent* result_event = (sys::ChatSendResultEvent*)event;
        if (state_ == State::Conversation && conversation_)
        {
            auto messages = service_.getRecentMessages(current_conv_, 50);
            conversation_->clearMessages();
            for (const auto& m : messages)
            {
                conversation_->addMessage(m);
            }
            conversation_->scrollToBottom();
        }
        (void)result_event;
        break;
    }

    case sys::EventType::ChatUnreadChanged:
    {
        refreshUnreadCounts();
        break;
    }

    default:
        break;
    }

    delete event;
}

void UiController::switchToChannelList()
{
    state_ = State::ChannelList;
    stopTeamConversationTimer();
    team_conv_active_ = false;
    Serial.printf("[UiController] switchToChannelList: parent=%p active=%p sleeping=%d\n",
                  parent_, lv_screen_active(), isScreenSleeping() ? 1 : 0);
    if (lv_obj_t* active = lv_screen_active()) {
        Serial.printf("[UiController] switchToChannelList active child count=%u\n",
                      (unsigned)lv_obj_get_child_cnt(active));
    }
    if (parent_) {
        Serial.printf("[UiController] switchToChannelList parent child count=%u\n",
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
    refreshUnreadCounts();
}

void UiController::switchToConversation(chat::ConversationId conv)
{
    state_ = State::Conversation;
    current_channel_ = conv.channel;
    current_conv_ = conv;
    team_conv_active_ = isTeamConversation(conv);
    stopTeamConversationTimer();
    Serial.printf("[UiController] switchToConversation: parent=%p active=%p sleeping=%d conv_peer=%08lX\n",
                  parent_, lv_screen_active(), isScreenSleeping() ? 1 : 0,
                  (unsigned long)conv.peer);
    if (lv_obj_t* active = lv_screen_active()) {
        Serial.printf("[UiController] switchToConversation active child count=%u\n",
                      (unsigned)lv_obj_get_child_cnt(active));
    }
    if (parent_) {
        Serial.printf("[UiController] switchToConversation parent child count=%u\n",
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

    if (!conversation_)
    {
        conversation_.reset(new ChatConversationScreen(parent_, conv));
        conversation_->setActionCallback(handle_conversation_action, this);
        conversation_->setBackCallback(handle_conversation_back, this);
    }

    if (team_conv_active_)
    {
        team::ui::TeamUiSnapshot snap;
        std::string title = "Team";
        if (team::ui::team_ui_get_store().load(snap))
        {
            title = team_title_from_snapshot(snap);
        }
        conversation_->setHeaderText(title.c_str(), nullptr);
        conversation_->updateBatteryFromBoard();
        refreshTeamConversation();
        startTeamConversationTimer();
        return;
    }

    // Update header (prefer contact name, else short_name)
    std::string title = "Broadcast";
    if (conv.peer != 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        std::string contact_name = app_ctx.getContactService().getContactName(conv.peer);
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
    conversation_->setHeaderText(title.c_str(), nullptr);
    conversation_->updateBatteryFromBoard();

    auto messages = service_.getRecentMessages(conv, 50);
    conversation_->clearMessages();
    for (const auto& msg : messages)
    {
        conversation_->addMessage(msg);
    }
    conversation_->scrollToBottom();
    service_.markConversationRead(conv);
}

void UiController::switchToCompose(chat::ConversationId conv)
{
    state_ = State::Compose;
    current_channel_ = conv.channel;
    current_conv_ = conv;
    team_conv_active_ = isTeamConversation(conv);
    stopTeamConversationTimer();
    Serial.printf("[UiController] switchToCompose: parent=%p active=%p sleeping=%d conv_peer=%08lX\n",
                  parent_, lv_screen_active(), isScreenSleeping() ? 1 : 0,
                  (unsigned long)conv.peer);
    if (lv_obj_t* active = lv_screen_active()) {
        Serial.printf("[UiController] switchToCompose active child count=%u\n",
                      (unsigned)lv_obj_get_child_cnt(active));
    }
    if (parent_) {
        Serial.printf("[UiController] switchToCompose parent child count=%u\n",
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

    std::string title = "Broadcast";
    if (team_conv_active_)
    {
        team::ui::TeamUiSnapshot snap;
        if (team::ui::team_ui_get_store().load(snap))
        {
            title = team_title_from_snapshot(snap);
        }
        else
        {
            title = "Team";
        }
        compose_->setHeaderText(title.c_str(), "RSSI --");
        compose_->setActionLabels("Send", "Cancel");
        compose_->setPositionButton("Position", true);
        return;
    }

    if (conv.peer != 0)
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        std::string contact_name = app_ctx.getContactService().getContactName(conv.peer);
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
    compose_->setHeaderText(title.c_str(), "RSSI --");
    compose_->setPositionButton(nullptr, false);
}

void UiController::handleChannelSelected(const chat::ConversationId& conv)
{
    switchToConversation(conv);
    if (!isTeamConversation(conv))
    {
        service_.switchChannel(conv.channel);
    }
}

void UiController::handleSendMessage(const std::string& text)
{
    if (text.empty())
    {
        return;
    }
    if (team_conv_active_)
    {
        return;
    }
    service_.sendText(current_channel_, text, current_conv_.peer);
}

void UiController::refreshUnreadCounts()
{
    if (!channel_list_)
    {
        return;
    }

    size_t total = 0;
    auto convs = service_.getConversations(0, 0, &total);

    // Update conversation names with contact nicknames
    app::AppContext& app_ctx = app::AppContext::getInstance();
    for (auto& conv : convs)
    {
        if (conv.id.peer != 0)
        {
            std::string contact_name = app_ctx.getContactService().getContactName(conv.id.peer);
            if (!contact_name.empty())
            {
                conv.name = contact_name;
            }
            // Otherwise keep the short_name from ConversationMeta
        }
    }

    team::ui::TeamUiSnapshot team_snap;
    if (team::ui::team_ui_get_store().load(team_snap) && team_snap.has_team_id)
    {
        chat::ConversationMeta team_conv;
        team_conv.id = teamConversationId();
        team_conv.name = team_title_from_snapshot(team_snap);
        team_conv.preview.clear();
        team_conv.last_timestamp = 0;
        team_conv.unread = 0;

        std::vector<team::ui::TeamChatLogEntry> entries;
        if (team::ui::team_ui_chatlog_load_recent(team_snap.team_id, 1, entries))
        {
            const auto& entry = entries.back();
            team_conv.preview = format_team_chat_entry(entry);
            team_conv.last_timestamp = entry.ts;
        }
        if (team_conv.preview.empty())
        {
            team_conv.preview = "No messages";
        }
        convs.insert(convs.begin(), team_conv);
    }

    channel_list_->setConversations(convs);
    channel_list_->setSelectedConversation(current_conv_);

    // Update header status (battery only, with icon)
    channel_list_->updateBatteryFromBoard();
}

bool UiController::isTeamConversation(const chat::ConversationId& conv) const
{
    return isTeamConversationId(conv);
}

void UiController::refreshTeamConversation()
{
    if (!conversation_ || !team_conv_active_)
    {
        return;
    }
    team::ui::TeamUiSnapshot snap;
    if (!team::ui::team_ui_get_store().load(snap) || !snap.has_team_id)
    {
        return;
    }
    conversation_->clearMessages();

    std::vector<team::ui::TeamChatLogEntry> entries;
    if (team::ui::team_ui_chatlog_load_recent(snap.team_id, 50, entries))
    {
        for (const auto& entry : entries)
        {
            chat::ChatMessage msg;
            msg.channel = chat::ChannelId::PRIMARY;
            msg.peer = 0;
            msg.from = entry.incoming ? entry.peer_id : 0;
            msg.timestamp = entry.ts;
            msg.text = format_team_chat_entry(entry);
            msg.status = entry.incoming ? chat::MessageStatus::Incoming : chat::MessageStatus::Sent;
            conversation_->addMessage(msg);
        }
    }
    conversation_->scrollToBottom();
}

void UiController::startTeamConversationTimer()
{
    if (team_conv_timer_)
    {
        lv_timer_resume(team_conv_timer_);
        return;
    }
    team_conv_timer_ = lv_timer_create([](lv_timer_t* timer) {
        auto* controller = static_cast<UiController*>(timer->user_data);
        if (controller)
        {
            controller->refreshTeamConversation();
        }
    }, 1000, this);
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

void UiController::handleConversationAction(ChatConversationScreen::ActionIntent intent)
{
    if (intent == ChatConversationScreen::ActionIntent::Reply) {
        switchToCompose(current_conv_);
    }
}

void UiController::handleComposeAction(ChatComposeScreen::ActionIntent intent)
{
    if (!compose_)
    {
        return;
    }
    if (intent == ChatComposeScreen::ActionIntent::Cancel)
    {
        switchToConversation(current_conv_);
        return;
    }

    if (team_conv_active_)
    {
        team::ui::TeamUiSnapshot snap;
        if (!team::ui::team_ui_get_store().load(snap) || !snap.has_team_id)
        {
            ::ui::SystemNotification::show("Team chat send failed", 2000);
            switchToConversation(current_conv_);
            return;
        }
        app::AppContext& app_ctx = app::AppContext::getInstance();
        team::TeamController* controller = app_ctx.getTeamController();
        if (!controller)
        {
            ::ui::SystemNotification::show("Team chat send failed", 2000);
            switchToConversation(current_conv_);
            return;
        }
        if (!snap.has_team_psk)
        {
            ::ui::SystemNotification::show("Team keys not ready", 2000);
            switchToConversation(current_conv_);
            return;
        }
        if (!controller->setKeysFromPsk(snap.team_id,
                                        snap.security_round,
                                        snap.team_psk.data(),
                                        snap.team_psk.size()))
        {
            ::ui::SystemNotification::show("Team keys not ready", 2000);
            switchToConversation(current_conv_);
            return;
        }

        uint32_t ts = static_cast<uint32_t>(time(nullptr));
        if (ts < 1577836800U)
        {
            ts = static_cast<uint32_t>(millis() / 1000);
        }

        if (intent == ChatComposeScreen::ActionIntent::Position)
        {
            std::string label = compose_->getText();
            gps::GpsState gps_state = gps::gps_get_data();
            if (!gps_state.valid)
            {
                ::ui::SystemNotification::show("No GPS fix", 2000);
                return;
            }

            team::proto::TeamChatLocation loc;
            loc.lat_e7 = static_cast<int32_t>(gps_state.lat * 1e7);
            loc.lon_e7 = static_cast<int32_t>(gps_state.lng * 1e7);
            if (gps_state.has_alt)
            {
                double alt = gps_state.alt_m;
                if (alt > 32767.0) alt = 32767.0;
                if (alt < -32768.0) alt = -32768.0;
                loc.alt_m = static_cast<int16_t>(lround(alt));
            }
            loc.ts = ts;
            if (!label.empty())
            {
                loc.label = label;
            }

            std::vector<uint8_t> payload;
            if (!team::proto::encodeTeamChatLocation(loc, payload))
            {
                ::ui::SystemNotification::show("Team location encode failed", 2000);
                switchToConversation(current_conv_);
                return;
            }

            team::proto::TeamChatMessage msg;
            msg.header.type = team::proto::TeamChatType::Location;
            msg.header.ts = ts;
            msg.header.msg_id = s_team_msg_id++;
            if (s_team_msg_id == 0)
            {
                s_team_msg_id = kTeamComposeMsgIdStart;
            }
            msg.payload = payload;

            bool ok = controller->onChat(msg, chat::ChannelId::PRIMARY);
            if (ok)
            {
                team::ui::team_ui_chatlog_append_structured(snap.team_id,
                                                            0,
                                                            false,
                                                            ts,
                                                            team::proto::TeamChatType::Location,
                                                            payload);
            }
            else
            {
                ::ui::SystemNotification::show("Team chat send failed", 2000);
            }
            switchToConversation(current_conv_);
            return;
        }

        std::string text = compose_->getText();
        if (text.empty())
        {
            switchToConversation(current_conv_);
            return;
        }
        team::proto::TeamChatMessage msg;
        msg.header.type = team::proto::TeamChatType::Text;
        msg.header.ts = ts;
        msg.header.msg_id = s_team_msg_id++;
        if (s_team_msg_id == 0)
        {
            s_team_msg_id = kTeamComposeMsgIdStart;
        }
        msg.payload.assign(text.begin(), text.end());

        bool ok = controller->onChat(msg, chat::ChannelId::PRIMARY);
        if (ok)
        {
            team::ui::team_ui_chatlog_append_structured(snap.team_id,
                                                        0,
                                                        false,
                                                        ts,
                                                        team::proto::TeamChatType::Text,
                                                        msg.payload);
        }
        else
        {
            ::ui::SystemNotification::show("Team chat send failed", 2000);
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
        }
    }
    switchToConversation(current_conv_);
}

void UiController::exitToMenu()
{
    // Clean up UI objects
    stopTeamConversationTimer();
    team_conv_active_ = false;
    service_.setModelEnabled(false);
    cleanupComposeIme();
    compose_.reset();
    conversation_.reset();
    channel_list_.reset();
    parent_ = nullptr;
    ui_request_exit_to_menu();
}

} // namespace ui
} // namespace chat

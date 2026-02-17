/**
 * @file ui_controller.cpp
 * @brief Chat UI controller implementation
 */

#include "ui_controller.h"
#include "../../app/app_context.h"
#include "../gps/gps_service_api.h"
#include "../sys/event_bus.h"
#include "../team/protocol/team_location_marker.h"
#include "screens/team/team_ui_store.h"
#include "ui_common.h"
#include "widgets/system_notification.h"
#include <Arduino.h>
#include <cmath>
#include <ctime>

extern "C"
{
    extern const lv_image_dsc_t AreaCleared;
    extern const lv_image_dsc_t BaseCamp;
    extern const lv_image_dsc_t GoodFind;
    extern const lv_image_dsc_t rally;
    extern const lv_image_dsc_t sos;
}

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
constexpr uint32_t kMinValidEpochSeconds = 1577836800U; // 2020-01-01

struct TeamPositionIconOption
{
    uint8_t icon_id;
    const char* meaning;
    const lv_image_dsc_t* image;
};

const TeamPositionIconOption kTeamPositionIconOptions[] = {
    {static_cast<uint8_t>(team::proto::TeamLocationMarkerIcon::AreaCleared),
     "Area Cleared", &AreaCleared},
    {static_cast<uint8_t>(team::proto::TeamLocationMarkerIcon::BaseCamp),
     "Base Camp", &BaseCamp},
    {static_cast<uint8_t>(team::proto::TeamLocationMarkerIcon::GoodFind),
     "Good Find", &GoodFind},
    {static_cast<uint8_t>(team::proto::TeamLocationMarkerIcon::Rally),
     "Rally Point", &rally},
    {static_cast<uint8_t>(team::proto::TeamLocationMarkerIcon::Sos),
     "Emergency SOS", &sos}};

const TeamPositionIconOption* find_team_position_icon_option(uint8_t icon_id)
{
    for (const auto& item : kTeamPositionIconOptions)
    {
        if (item.icon_id == icon_id)
        {
            return &item;
        }
    }
    return nullptr;
}

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
    switch (type)
    {
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
    if (text.size() <= max_len)
    {
        return text;
    }
    if (max_len <= 3)
    {
        return text.substr(0, max_len);
    }
    return text.substr(0, max_len - 3) + "...";
}

std::string format_team_chat_entry(const team::ui::TeamChatLogEntry& entry)
{
    if (entry.type == team::proto::TeamChatType::Text)
    {
        std::string text(entry.payload.begin(), entry.payload.end());
        return truncate_text(text, 160);
    }
    if (entry.type == team::proto::TeamChatType::Location)
    {
        team::proto::TeamChatLocation loc;
        if (team::proto::decodeTeamChatLocation(entry.payload.data(),
                                                entry.payload.size(),
                                                &loc))
        {
            double lat = static_cast<double>(loc.lat_e7) / 1e7;
            double lon = static_cast<double>(loc.lon_e7) / 1e7;
            char buf[128];
            char coord_buf[64];
            uint8_t coord_fmt = app::AppContext::getInstance().getConfig().gps_coord_format;
            ui_format_coords(lat, lon, coord_fmt, coord_buf, sizeof(coord_buf));
            const bool has_marker_icon = team::proto::team_location_marker_icon_is_valid(loc.source);
            const char* marker_name = team::proto::team_location_marker_icon_name(loc.source);
            if (has_marker_icon)
            {
                snprintf(buf, sizeof(buf), "%s: %s", marker_name, coord_buf);
            }
            else if (!loc.label.empty())
            {
                snprintf(buf, sizeof(buf), "Location: %s %s",
                         loc.label.c_str(), coord_buf);
            }
            else
            {
                snprintf(buf, sizeof(buf), "Location: %s", coord_buf);
            }
            return std::string(buf);
        }
        return "Location";
    }
    if (entry.type == team::proto::TeamChatType::Command)
    {
        team::proto::TeamChatCommand cmd;
        if (team::proto::decodeTeamChatCommand(entry.payload.data(),
                                               entry.payload.size(),
                                               &cmd))
        {
            const char* name = team_command_name(cmd.cmd_type);
            double lat = static_cast<double>(cmd.lat_e7) / 1e7;
            double lon = static_cast<double>(cmd.lon_e7) / 1e7;
            char buf[160];
            char coord_buf[64];
            uint8_t coord_fmt = app::AppContext::getInstance().getConfig().gps_coord_format;
            ui_format_coords(lat, lon, coord_fmt, coord_buf, sizeof(coord_buf));
            if (cmd.lat_e7 != 0 || cmd.lon_e7 != 0)
            {
                if (!cmd.note.empty())
                {
                    snprintf(buf, sizeof(buf), "Command: %s %s %s",
                             name, coord_buf, cmd.note.c_str());
                }
                else
                {
                    snprintf(buf, sizeof(buf), "Command: %s %s",
                             name, coord_buf);
                }
            }
            else if (!cmd.note.empty())
            {
                snprintf(buf, sizeof(buf), "Command: %s %s", name, cmd.note.c_str());
            }
            else
            {
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
    if (!snap.team_name.empty())
    {
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

UiController::UiController(lv_obj_t* parent, chat::ChatService& service, chat::ChannelId initial_channel)
    : parent_(parent), service_(service), state_(State::ChannelList),
      current_channel_(initial_channel),
      current_conv_(chat::ConversationId(initial_channel, 0))
{
}

UiController::~UiController()
{
    closeTeamPositionPicker(false);
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
    switchToChannelList();
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
    closeTeamPositionPicker(false);
    state_ = State::ChannelList;
    stopTeamConversationTimer();
    team_conv_active_ = false;
    Serial.printf("[UiController] switchToChannelList: parent=%p active=%p sleeping=%d\n",
                  parent_, lv_screen_active(), isScreenSleeping() ? 1 : 0);
    if (lv_obj_t* active = lv_screen_active())
    {
        Serial.printf("[UiController] switchToChannelList active child count=%u\n",
                      (unsigned)lv_obj_get_child_cnt(active));
    }
    if (parent_)
    {
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
    closeTeamPositionPicker(false);
    state_ = State::Conversation;
    current_channel_ = conv.channel;
    current_conv_ = conv;
    team_conv_active_ = isTeamConversation(conv);
    stopTeamConversationTimer();
    Serial.printf("[UiController] switchToConversation: parent=%p active=%p sleeping=%d conv_peer=%08lX\n",
                  parent_, lv_screen_active(), isScreenSleeping() ? 1 : 0,
                  (unsigned long)conv.peer);
    if (lv_obj_t* active = lv_screen_active())
    {
        Serial.printf("[UiController] switchToConversation active child count=%u\n",
                      (unsigned)lv_obj_get_child_cnt(active));
    }
    if (parent_)
    {
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

    if (team_conv_active_)
    {
        team::ui::TeamUiSnapshot snap;
        std::string title = "Team";
        bool loaded = team::ui::team_ui_get_store().load(snap);
        if (loaded)
        {
            title = team_title_from_snapshot(snap);
        }
        conversation_->setHeaderText(title.c_str(), nullptr);
        conversation_->updateBatteryFromBoard();
        refreshTeamConversation();
        startTeamConversationTimer();
        if (loaded && snap.team_chat_unread != 0)
        {
            snap.team_chat_unread = 0;
            team::ui::team_ui_get_store().save(snap);
            sys::EventBus::publish(new sys::ChatUnreadChangedEvent(kTeamChatChannelRaw, 0), 0);
        }
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
    closeTeamPositionPicker(false);
    state_ = State::Compose;
    current_channel_ = conv.channel;
    current_conv_ = conv;
    team_conv_active_ = isTeamConversation(conv);
    stopTeamConversationTimer();
    Serial.printf("[UiController] switchToCompose: parent=%p active=%p sleeping=%d conv_peer=%08lX\n",
                  parent_, lv_screen_active(), isScreenSleeping() ? 1 : 0,
                  (unsigned long)conv.peer);
    if (lv_obj_t* active = lv_screen_active())
    {
        Serial.printf("[UiController] switchToCompose active child count=%u\n",
                      (unsigned)lv_obj_get_child_cnt(active));
    }
    if (parent_)
    {
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
        compose_->setHeaderText(title.c_str(), nullptr);
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
    compose_->setHeaderText(title.c_str(), nullptr);
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
        team_conv.unread = static_cast<int>(team_snap.team_chat_unread);

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
            if (entry.type == team::proto::TeamChatType::Location)
            {
                team::proto::TeamChatLocation loc;
                if (team::proto::decodeTeamChatLocation(entry.payload.data(),
                                                        entry.payload.size(),
                                                        &loc))
                {
                    if (team::proto::team_location_marker_icon_is_valid(loc.source))
                    {
                        msg.team_location_icon = loc.source;
                    }
                    msg.has_geo = true;
                    msg.geo_lat_e7 = loc.lat_e7;
                    msg.geo_lon_e7 = loc.lon_e7;
                }
            }
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
    team_conv_timer_ = lv_timer_create([](lv_timer_t* timer)
                                       {
        auto* controller = static_cast<UiController*>(timer->user_data);
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
    return team_position_picker_overlay_ != nullptr;
}

void UiController::updateTeamPositionPickerHint(uint8_t icon_id)
{
    if (!team_position_picker_desc_)
    {
        return;
    }
    if (icon_id == 0)
    {
        lv_label_set_text(team_position_picker_desc_, "Cancel");
        return;
    }
    const TeamPositionIconOption* option = find_team_position_icon_option(icon_id);
    if (option)
    {
        lv_label_set_text(team_position_picker_desc_, option->meaning);
    }
    else
    {
        lv_label_set_text(team_position_picker_desc_, "Select marker");
    }
}

void UiController::team_position_icon_event_cb(lv_event_t* e)
{
    auto* ctx = static_cast<TeamPositionIconEventCtx*>(lv_event_get_user_data(e));
    if (!ctx || !ctx->controller)
    {
        return;
    }
    UiController* controller = ctx->controller;
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED)
    {
        controller->updateTeamPositionPickerHint(ctx->icon_id);
        lv_obj_scroll_to_view(lv_event_get_target_obj(e), LV_ANIM_ON);
        return;
    }
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
        controller->onTeamPositionIconSelected(ctx->icon_id);
    }
}

void UiController::team_position_cancel_event_cb(lv_event_t* e)
{
    auto* controller = static_cast<UiController*>(lv_event_get_user_data(e));
    if (!controller)
    {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED)
    {
        controller->updateTeamPositionPickerHint(0);
        return;
    }
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
        controller->onTeamPositionCancel();
    }
}

void UiController::openTeamPositionPicker()
{
    if (!team_conv_active_ || !compose_ || isTeamPositionPickerOpen() || !parent_)
    {
        return;
    }

    team_position_prev_group_ = lv_group_get_default();
    team_position_picker_group_ = lv_group_create();
    set_default_group(team_position_picker_group_);

    team_position_picker_overlay_ = lv_obj_create(parent_);
    lv_obj_set_size(team_position_picker_overlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(team_position_picker_overlay_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(team_position_picker_overlay_, LV_OPA_50, 0);
    lv_obj_set_style_border_width(team_position_picker_overlay_, 0, 0);
    lv_obj_set_style_pad_all(team_position_picker_overlay_, 0, 0);
    lv_obj_set_style_radius(team_position_picker_overlay_, 0, 0);
    lv_obj_clear_flag(team_position_picker_overlay_, LV_OBJ_FLAG_SCROLLABLE);

    team_position_picker_panel_ = lv_obj_create(team_position_picker_overlay_);
    lv_obj_set_size(team_position_picker_panel_, LV_PCT(92), 186);
    lv_obj_center(team_position_picker_panel_);
    lv_obj_set_style_bg_color(team_position_picker_panel_, lv_color_hex(0xFAF0D8), 0);
    lv_obj_set_style_bg_opa(team_position_picker_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(team_position_picker_panel_, 1, 0);
    lv_obj_set_style_border_color(team_position_picker_panel_, lv_color_hex(0xE7C98F), 0);
    lv_obj_set_style_radius(team_position_picker_panel_, 10, 0);
    lv_obj_set_style_pad_all(team_position_picker_panel_, 10, 0);
    lv_obj_clear_flag(team_position_picker_panel_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_bar = lv_obj_create(team_position_picker_panel_);
    lv_obj_set_size(title_bar, LV_PCT(100), 28);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0xEBA341), 0);
    lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(title_bar, 0, 0);
    lv_obj_set_style_radius(title_bar, 6, 0);
    lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, -4);
    lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(title_bar);
    lv_label_set_text(title, "Share Position Marker");
    lv_obj_set_style_text_color(title, lv_color_hex(0x6B4A1E), 0);
    lv_obj_center(title);

    lv_obj_t* icon_row = lv_obj_create(team_position_picker_panel_);
    lv_obj_set_size(icon_row, LV_PCT(100), 72);
    lv_obj_set_style_bg_opa(icon_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon_row, 0, 0);
    lv_obj_set_style_pad_all(icon_row, 0, 0);
    lv_obj_set_style_pad_column(icon_row, 6, 0);
    lv_obj_set_flex_flow(icon_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(icon_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(icon_row, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_set_scroll_dir(icon_row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(icon_row, LV_SCROLLBAR_MODE_OFF);

    for (const auto& option : kTeamPositionIconOptions)
    {
        lv_obj_t* btn = lv_btn_create(icon_row);
        lv_obj_set_size(btn, 60, 60);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xF6E6C6), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn, lv_color_hex(0xE7C98F), LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
        lv_obj_set_style_outline_width(btn, 2, LV_PART_MAIN | LV_STATE_FOCUSED);
        lv_obj_set_style_outline_color(btn, lv_color_hex(0xC98118), LV_PART_MAIN | LV_STATE_FOCUSED);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* img = lv_image_create(btn);
        lv_image_set_src(img, option.image);
        lv_obj_center(img);

        auto* ctx = new TeamPositionIconEventCtx();
        ctx->controller = this;
        ctx->icon_id = option.icon_id;
        team_position_icon_ctxs_.push_back(ctx);

        lv_obj_add_event_cb(btn, team_position_icon_event_cb, LV_EVENT_CLICKED, ctx);
        lv_obj_add_event_cb(btn, team_position_icon_event_cb, LV_EVENT_KEY, ctx);
        lv_obj_add_event_cb(btn, team_position_icon_event_cb, LV_EVENT_FOCUSED, ctx);

        lv_group_add_obj(team_position_picker_group_, btn);
    }

    team_position_picker_desc_ = lv_label_create(team_position_picker_panel_);
    lv_label_set_text(team_position_picker_desc_, "Select marker");
    lv_obj_set_width(team_position_picker_desc_, LV_PCT(100));
    lv_obj_set_style_text_align(team_position_picker_desc_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(team_position_picker_desc_, lv_color_hex(0x8A6A3A), 0);
    lv_obj_align(team_position_picker_desc_, LV_ALIGN_TOP_MID, 0, 104);

    lv_obj_t* cancel_btn = lv_btn_create(team_position_picker_panel_);
    lv_obj_set_size(cancel_btn, 96, 28);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0xF6E6C6), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cancel_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(cancel_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(cancel_btn, lv_color_hex(0xE7C98F), LV_PART_MAIN);
    lv_obj_set_style_radius(cancel_btn, 6, LV_PART_MAIN);
    lv_obj_set_style_outline_width(cancel_btn, 2, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(cancel_btn, lv_color_hex(0xC98118), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_clear_flag(cancel_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_color(cancel_label, lv_color_hex(0x6B4A1E), 0);
    lv_obj_center(cancel_label);

    lv_obj_add_event_cb(cancel_btn, team_position_cancel_event_cb, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(cancel_btn, team_position_cancel_event_cb, LV_EVENT_KEY, this);
    lv_obj_add_event_cb(cancel_btn, team_position_cancel_event_cb, LV_EVENT_FOCUSED, this);
    lv_group_add_obj(team_position_picker_group_, cancel_btn);

    if (!team_position_icon_ctxs_.empty() && team_position_picker_group_)
    {
        lv_obj_t* first_btn = lv_group_get_focused(team_position_picker_group_);
        if (!first_btn)
        {
            first_btn = lv_obj_get_child(icon_row, 0);
        }
        if (first_btn)
        {
            lv_group_focus_obj(first_btn);
        }
        updateTeamPositionPickerHint(team_position_icon_ctxs_.front()->icon_id);
    }
    lv_obj_move_foreground(team_position_picker_overlay_);
}

void UiController::closeTeamPositionPicker(bool restore_group)
{
    for (auto* ctx : team_position_icon_ctxs_)
    {
        delete ctx;
    }
    team_position_icon_ctxs_.clear();

    if (team_position_picker_group_ && lv_group_get_default() == team_position_picker_group_)
    {
        if (restore_group && team_position_prev_group_)
        {
            set_default_group(team_position_prev_group_);
        }
        else
        {
            set_default_group(nullptr);
        }
    }

    if (team_position_picker_overlay_ && lv_obj_is_valid(team_position_picker_overlay_))
    {
        lv_obj_del(team_position_picker_overlay_);
    }

    if (team_position_picker_group_)
    {
        lv_group_del(team_position_picker_group_);
    }

    team_position_picker_overlay_ = nullptr;
    team_position_picker_panel_ = nullptr;
    team_position_picker_desc_ = nullptr;
    team_position_picker_group_ = nullptr;
    team_position_prev_group_ = nullptr;
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

    team::ui::TeamUiSnapshot snap;
    if (!team::ui::team_ui_get_store().load(snap) || !snap.has_team_id)
    {
        ::ui::SystemNotification::show("Team chat send failed", 2000);
        return false;
    }
    app::AppContext& app_ctx = app::AppContext::getInstance();
    team::TeamController* controller = app_ctx.getTeamController();
    if (!controller)
    {
        ::ui::SystemNotification::show("Team chat send failed", 2000);
        return false;
    }
    if (!snap.has_team_psk)
    {
        ::ui::SystemNotification::show("Team keys not ready", 2000);
        return false;
    }
    if (!controller->setKeysFromPsk(snap.team_id,
                                    snap.security_round,
                                    snap.team_psk.data(),
                                    snap.team_psk.size()))
    {
        ::ui::SystemNotification::show("Team keys not ready", 2000);
        return false;
    }

    gps::GpsState gps_state = gps::gps_get_data();
    if (!gps_state.valid)
    {
        ::ui::SystemNotification::show("No GPS fix", 2000);
        return false;
    }

    uint32_t ts = static_cast<uint32_t>(time(nullptr));
    if (ts < kMinValidEpochSeconds)
    {
        ts = static_cast<uint32_t>(millis() / 1000);
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
    loc.source = icon_id;
    loc.label = team::proto::team_location_marker_icon_name(icon_id);

    std::vector<uint8_t> payload;
    if (!team::proto::encodeTeamChatLocation(loc, payload))
    {
        ::ui::SystemNotification::show("Team location encode failed", 2000);
        return false;
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
    return ok;
}

void UiController::onTeamPositionIconSelected(uint8_t icon_id)
{
    closeTeamPositionPicker(false);
    sendTeamLocationWithIcon(icon_id);
    switchToConversation(current_conv_);
}

void UiController::handleConversationAction(ChatConversationScreen::ActionIntent intent)
{
    if (intent == ChatConversationScreen::ActionIntent::Reply)
    {
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

        if (intent == ChatComposeScreen::ActionIntent::Position)
        {
            openTeamPositionPicker();
            return;
        }

        uint32_t ts = static_cast<uint32_t>(time(nullptr));
        if (ts < kMinValidEpochSeconds)
        {
            ts = static_cast<uint32_t>(millis() / 1000);
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
    if (exiting_)
    {
        return;
    }
    closeTeamPositionPicker(false);
    exiting_ = true;
    stopTeamConversationTimer();
    team_conv_active_ = false;
    ui_request_exit_to_menu();
}

} // namespace ui
} // namespace chat

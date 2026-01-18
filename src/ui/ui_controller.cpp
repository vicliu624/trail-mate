/**
 * @file ui_controller.cpp
 * @brief Chat UI controller implementation
 */

#include "ui_controller.h"
#include "../sys/event_bus.h"
#include "ui_common.h"
#include "../../app/app_context.h"
#include <ctime>

namespace chat {
namespace ui {

namespace {
void handle_channel_click(chat::ChannelId channel, void* user_data)
{
    auto* controller = static_cast<UiController*>(user_data);
    if (controller) {
        controller->onChannelClicked(channel);
    }
}

void handle_conversation_action(bool compose, void* user_data)
{
    auto* controller = static_cast<UiController*>(user_data);
    if (controller) {
        controller->handleConversationAction(compose);
    }
}

void handle_compose_back(void* user_data)
{
    auto* controller = static_cast<UiController*>(user_data);
    if (controller) {
        controller->handleComposeAction(false);
    }
}

void handle_compose_action(bool send, void* user_data)
{
    auto* controller = static_cast<UiController*>(user_data);
    if (controller) {
        controller->handleComposeAction(send);
    }
}

void handle_back(void* user_data)
{
    auto* controller = static_cast<UiController*>(user_data);
    if (controller) {
        controller->exitToMenu();
    }
}

void handle_conversation_back(void* user_data)
{
    auto* controller = static_cast<UiController*>(user_data);
    if (controller) {
        controller->backToList();
    }
}
} // namespace

UiController::UiController(lv_obj_t* parent, chat::ChatService& service)
    : parent_(parent), service_(service), state_(State::ChannelList),
      current_channel_(chat::ChannelId::PRIMARY),
      current_conv_(chat::ConversationId(chat::ChannelId::PRIMARY, 0)) {
}

UiController::~UiController() {
    channel_list_.reset();
    conversation_.reset();
    cleanupComposeIme();
    compose_.reset();
}

void UiController::cleanupComposeIme() {
    if (compose_ime_) {
        compose_ime_->detach();
        compose_ime_.reset();
    }
}

void UiController::init() {
    switchToChannelList();
    refreshUnreadCounts();
}

void UiController::update() {
    // Process incoming messages
    service_.processIncoming();
    
    // Refresh UI if needed
    if (state_ == State::ChannelList && channel_list_) {
        refreshUnreadCounts();
    }
}

void UiController::onChannelClicked(chat::ChannelId channel)
{
    (void)channel;
    if (channel_list_) {
        handleChannelSelected(channel_list_->getSelectedConversation());
    }
}

void UiController::backToList() {
    switchToChannelList();
}

void UiController::onInput(const sys::InputEvent& event) {
    switch (state_) {
        case State::ChannelList:
            if (event.input_type == sys::InputEvent::RotaryTurn) {
                // Handle rotary navigation
                // (Implementation depends on rotary event details)
            } else if (event.input_type == sys::InputEvent::RotaryPress) {
                if (channel_list_) {
                    handleChannelSelected(channel_list_->getSelectedConversation());
                }
            } else if (event.input_type == sys::InputEvent::KeyPress && event.value == 27) {
                // ESC - return to main menu (handled by parent)
            }
            break;
            
        case State::Conversation:
            if (event.input_type == sys::InputEvent::KeyPress && event.value == 27) {
                // ESC - return to channel list
                switchToChannelList();
            }
            break;

        case State::Compose:
            if (event.input_type == sys::InputEvent::KeyPress && event.value == 27) {
                // ESC - cancel compose
                switchToConversation(current_conv_);
            }
            break;
            
        default:
            break;
    }
}

void UiController::onChatEvent(sys::Event* event) {
    if (!event) {
        return;
    }
    
    switch (event->type) {
        case sys::EventType::ChatNewMessage: {
            sys::ChatNewMessageEvent* msg_event = (sys::ChatNewMessageEvent*)event;
            Serial.printf("[UiController::onChatEvent] ChatNewMessage received: channel=%d, state=%d, current_channel=%d\n",
                         msg_event->channel, (int)state_, (int)current_channel_);
            
            // Note: Haptic feedback is now handled globally in AppContext::update()
            // No need to call vibrator() here
            
            if (state_ == State::Conversation &&
                (uint8_t)current_channel_ == msg_event->channel) {
                Serial.printf("[UiController::onChatEvent] Updating conversation UI...\n");
                auto messages = service_.getRecentMessages(current_conv_, 50);
                conversation_->clearMessages();
                for (const auto& m : messages) {
                    conversation_->addMessage(m);
                }
                conversation_->scrollToBottom();
            }
            refreshUnreadCounts();
            break;
        }
        
        case sys::EventType::ChatSendResult: {
            sys::ChatSendResultEvent* result_event = (sys::ChatSendResultEvent*)event;
            // Update message status in conversation
            // (Would need to track message indices)
            break;
        }
        
        case sys::EventType::ChatUnreadChanged: {
            refreshUnreadCounts();
            break;
        }
        
        default:
            break;
    }
    
    delete event;
}

void UiController::switchToChannelList() {
    state_ = State::ChannelList;
    
    if (conversation_) {
        conversation_.reset();
    }
    if (compose_) {
        cleanupComposeIme();
        compose_.reset();
    }
    
    if (!channel_list_) {
        channel_list_.reset(new ChatMessageListScreen(parent_));
        channel_list_->setChannelSelectCallback(handle_channel_click, this);
        channel_list_->setBackCallback(handle_back, this);
    }
    
    refreshUnreadCounts();
}

void UiController::switchToConversation(chat::ConversationId conv) {
    state_ = State::Conversation;
    current_channel_ = conv.channel;
    current_conv_ = conv;
    
    if (channel_list_) {
        channel_list_.reset();
    }
    if (compose_) {
        cleanupComposeIme();
        compose_.reset();
    }
    
    if (!conversation_) {
        conversation_.reset(new ChatConversationScreen(parent_, conv));
        conversation_->setActionCallback(handle_conversation_action, this);
        conversation_->setBackCallback(handle_conversation_back, this);
    }
    // 更新标题（优先使用联系人昵称，否则使用short_name）
    std::string title = "Broadcast";
    if (conv.peer != 0) {
        // Try to get contact name first
        app::AppContext& app_ctx = app::AppContext::getInstance();
        std::string contact_name = app_ctx.getContactService().getContactName(conv.peer);
        if (!contact_name.empty()) {
            title = contact_name;
        } else {
            // Fallback to short_name from conversation meta
            auto convs = service_.getConversations();
            for (const auto& c : convs) {
                if (c.id == conv) {
                    title = c.name;
                    break;
                }
            }
        }
    }
    conversation_->setHeaderText(title.c_str(), nullptr);
    conversation_->updateBatteryFromBoard();
    
    // Load recent messages
    auto messages = service_.getRecentMessages(conv, 50);
    conversation_->clearMessages();
    for (const auto& msg : messages) {
        conversation_->addMessage(msg);
    }
    conversation_->scrollToBottom();
    
    // Mark as read
    service_.markConversationRead(conv);
}

void UiController::switchToCompose(chat::ConversationId conv) {
    state_ = State::Compose;
    current_channel_ = conv.channel;
    current_conv_ = conv;

    if (channel_list_) {
        channel_list_.reset();
    }
    if (conversation_) {
        conversation_.reset();
    }

    if (!compose_) {
        compose_.reset(new ChatComposeScreen(parent_, conv));
        compose_->setActionCallback(handle_compose_action, this);
        compose_->setBackCallback(handle_compose_back, this);
    }

    lv_obj_t* compose_content = compose_->getContent();
    lv_obj_t* compose_textarea = compose_->getTextarea();
    if (compose_content && compose_textarea) {
        if (compose_ime_) {
            compose_ime_->detach();
        } else {
            compose_ime_.reset(new ::ui::widgets::ImeWidget());
        }
        compose_ime_->init(compose_content, compose_textarea);
        compose_->attachImeWidget(compose_ime_.get());
        if (lv_group_t* g = lv_group_get_default()) {
            lv_group_add_obj(g, compose_ime_->focus_obj());
        }
    }
    // 更新 Compose 页头：优先使用联系人昵称，否则使用short_name
    std::string title = "Broadcast";
    if (conv.peer != 0) {
        // Try to get contact name first
        app::AppContext& app_ctx = app::AppContext::getInstance();
        std::string contact_name = app_ctx.getContactService().getContactName(conv.peer);
        if (!contact_name.empty()) {
            title = contact_name;
        } else {
            // Fallback to short_name from conversation meta
            auto convs = service_.getConversations();
            for (const auto& c : convs) {
                if (c.id == conv) {
                    title = c.name;
                    break;
                }
            }
        }
    }
    compose_->setHeaderText(title.c_str(), "RSSI --");
}

void UiController::handleChannelSelected(const chat::ConversationId& conv) {
    switchToConversation(conv);
    service_.switchChannel(conv.channel);
}

void UiController::handleSendMessage(const std::string& text) {
    if (text.empty()) {
        return;
    }
    
    service_.sendText(current_channel_, text, current_conv_.peer);
}

void UiController::refreshUnreadCounts() {
    if (!channel_list_) {
        return;
    }

    auto convs = service_.getConversations();
    
    // Update conversation names with contact nicknames
    app::AppContext& app_ctx = app::AppContext::getInstance();
    for (auto& conv : convs) {
        if (conv.id.peer != 0) {
            std::string contact_name = app_ctx.getContactService().getContactName(conv.id.peer);
            if (!contact_name.empty()) {
                conv.name = contact_name;
            }
            // Otherwise keep the short_name from ConversationMeta
        }
    }
    
    channel_list_->setConversations(convs);
    for (size_t i = 0; i < convs.size(); ++i) {
        if (convs[i].id == current_conv_) {
            channel_list_->setSelected(static_cast<int>(i));
            break;
        }
    }

    // Update header status (battery only, with icon)
    channel_list_->updateBatteryFromBoard();
}

void UiController::handleConversationAction(bool compose) {
    (void)compose;
    switchToCompose(current_conv_);
}

void UiController::handleComposeAction(bool send) {
    if (!compose_) {
        return;
    }
    if (!send) {
        switchToConversation(current_conv_);
        return;
    }
    std::string text = compose_->getText();
    if (!text.empty()) {
        handleSendMessage(text);
    }
    switchToConversation(current_conv_);
}

void UiController::exitToMenu() {
    // Clean up UI objects
    cleanupComposeIme();
    compose_.reset();
    conversation_.reset();
    channel_list_.reset();
    if (parent_) {
        lv_obj_del(parent_);
        parent_ = nullptr;
    }
    menu_show();
}

} // namespace ui
} // namespace chat

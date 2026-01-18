/**
 * @file chat_conversation.cpp
 * @brief Chat conversation screen implementation
 */
#include "chat_conversation_components.h"

#include "../ui_common.h"
#include "chat_conversation_layout.h"
#include "chat_conversation_styles.h"
#include "chat_conversation_input.h"

#include <cstring>
#include <ctime>
#include <cstdio>

namespace chat {
namespace ui {

// Keep original constant meaning (bubble max width and 70% rule)
namespace {
constexpr lv_coord_t kBubbleMaxWidth = 322; // same as original
} // namespace

ChatConversationScreen::ChatConversationScreen(lv_obj_t* parent, chat::ConversationId conv)
    : conv_(conv)
{
    // ----- Layout -----
    auto w = chat::ui::layout::create_conversation_base(parent);
    container_ = w.root;
    msg_list_ = w.msg_list;
    action_bar_ = w.action_bar;
    reply_btn_ = w.reply_btn;

    // ----- Styles -----
    chat::ui::conversation::styles::apply_root(container_);
    chat::ui::conversation::styles::apply_msg_list(msg_list_);
    chat::ui::conversation::styles::apply_action_bar(action_bar_);
    chat::ui::conversation::styles::apply_reply_btn(reply_btn_);

    // Reply label text + style
    lv_label_set_text(w.reply_label, "Reply");
    chat::ui::conversation::styles::apply_reply_label(w.reply_label);

    // ----- Top bar (existing widget, unchanged behavior) -----
    ::ui::widgets::top_bar_init(top_bar_, w.topbar_host);
    const char* title = (conv_.peer == 0) ? "Broadcast" : "Direct";
    ::ui::widgets::top_bar_set_title(top_bar_, title);
    ::ui::widgets::top_bar_set_right_text(top_bar_, "");
    ::ui::widgets::top_bar_set_back_callback(top_bar_, handle_back, this);

    // ----- Event (unchanged) -----
    lv_obj_add_event_cb(reply_btn_, action_event_cb, LV_EVENT_CLICKED, this);

    // ----- Input layer (explicit, v0 no-op) -----
    chat::ui::conversation::input::init(this);
}

ChatConversationScreen::~ChatConversationScreen()
{
    chat::ui::conversation::input::cleanup();
    if (container_) {
        lv_obj_del(container_);
    }
}

void ChatConversationScreen::addMessage(const chat::ChatMessage& msg)
{
    if (messages_.size() >= MAX_DISPLAY_MESSAGES) {
        MessageItem& oldest = messages_[0];
        if (oldest.container) {
            lv_obj_del(oldest.container);
        }
        messages_.erase(messages_.begin());
    }

    createMessageItem(msg);
    scrollToBottom();
}

void ChatConversationScreen::clearMessages()
{
    for (auto& item : messages_) {
        if (item.container) {
            lv_obj_del(item.container);
        }
    }
    messages_.clear();
}

void ChatConversationScreen::scrollToBottom()
{
    if (msg_list_) {
        lv_obj_scroll_to_y(msg_list_, LV_COORD_MAX, LV_ANIM_OFF);
    }
}

void ChatConversationScreen::setActionCallback(void (*cb)(bool compose, void*), void* user_data)
{
    action_cb_ = cb;
    action_cb_user_data_ = user_data;
}

void ChatConversationScreen::setHeaderText(const char* title, const char* status)
{
    ::ui::widgets::top_bar_set_title(top_bar_, title);
    if (status != nullptr) {
        ::ui::widgets::top_bar_set_right_text(top_bar_, status);
    }
}

void ChatConversationScreen::updateBatteryFromBoard()
{
    ui_update_top_bar_battery(top_bar_);
}

void ChatConversationScreen::setBackCallback(void (*cb)(void*), void* user_data)
{
    back_cb_ = cb;
    back_cb_user_data_ = user_data;
}

void ChatConversationScreen::createMessageItem(const chat::ChatMessage& msg)
{
    MessageItem item;
    item.msg = msg;

    // ----- Layout: row + bubble + text -----
    item.container = chat::ui::layout::create_message_row(msg_list_);
    chat::ui::conversation::styles::apply_message_row(item.container);

    const bool is_self = (msg.from == 0);

    // Compute bubble max width (same logic as original)
    lv_coord_t max_bubble_w = kBubbleMaxWidth;
    lv_coord_t list_w = chat::ui::layout::get_msg_list_content_width(msg_list_);
    if (list_w > 0) {
        // original: candidate = (list_w - 2 * kPadX) * 7 / 10
        // kPadX lives in styles; but for behavior parity we replicate formula using known 8px.
        constexpr lv_coord_t kPadX = 8;
        lv_coord_t candidate = (list_w - 2 * kPadX) * 7 / 10;
        if (candidate > 0 && candidate < max_bubble_w) {
            max_bubble_w = candidate;
        }
    }

    lv_obj_t* bubble = chat::ui::layout::create_bubble(item.container);
    chat::ui::conversation::styles::apply_bubble(bubble, is_self);
    chat::ui::layout::set_bubble_max_width(bubble, max_bubble_w);

    item.text_label = chat::ui::layout::create_bubble_text(bubble);
    lv_label_set_text(item.text_label, msg.text.c_str());
    chat::ui::conversation::styles::apply_bubble_text(item.text_label);

    // Align row based on sender (same behavior)
    chat::ui::layout::align_message_row(item.container, is_self);

    messages_.push_back(item);
}

void ChatConversationScreen::action_event_cb(lv_event_t* e)
{
    ChatConversationScreen* screen =
        static_cast<ChatConversationScreen*>(lv_event_get_user_data(e));
    if (!screen || !screen->action_cb_) {
        return;
    }

    // Behavior parity: only Reply remains -> compose=false
    (void)lv_event_get_target(e);
    bool compose = false;
    screen->action_cb_(compose, screen->action_cb_user_data_);
}

void ChatConversationScreen::handle_back(void* user_data)
{
    ChatConversationScreen* screen = static_cast<ChatConversationScreen*>(user_data);
    if (screen != nullptr && screen->back_cb_ != nullptr) {
        screen->back_cb_(screen->back_cb_user_data_);
    }
}

} // namespace ui
} // namespace chat

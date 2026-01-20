/**
 * @file chat_conversation.cpp
 * @brief Chat conversation screen implementation
 */
#include "chat_conversation_components.h"

#include "../ui_common.h"
#include "../../../app/app_context.h"
#include "chat_conversation_layout.h"
#include "chat_conversation_styles.h"
#include "chat_conversation_input.h"

#include <Arduino.h>
#include <ctime>
#include <cstdio>

namespace chat {
namespace ui {

// Keep original constant meaning (bubble max width and 70% rule)
namespace {
constexpr lv_coord_t kBubbleMaxWidth = 322; // same as original
constexpr uint32_t kSecondsPerDay = 24U * 60U * 60U;
constexpr uint32_t kSecondsPerMonth = 30U * kSecondsPerDay;
constexpr uint32_t kSecondsPerYear = 365U * kSecondsPerDay;
constexpr uint32_t kMinValidEpochSeconds = 1577836800U; // 2020-01-01
} // namespace

static bool is_valid_epoch_ts(uint32_t ts)
{
    return ts >= kMinValidEpochSeconds;
}

static void format_hms(char* out, size_t out_len, uint32_t seconds)
{
    unsigned h = static_cast<unsigned>(seconds / 3600U);
    unsigned m = static_cast<unsigned>((seconds / 60U) % 60U);
    unsigned s = static_cast<unsigned>(seconds % 60U);
    snprintf(out, out_len, "%02u:%02u:%02u", h, m, s);
}

static void format_message_time(char* out, size_t out_len, uint32_t ts)
{
    if (!out || out_len == 0) return;
    if (ts == 0) {
        snprintf(out, out_len, "--");
        return;
    }

    uint32_t now_epoch = static_cast<uint32_t>(time(nullptr));
    bool ts_is_epoch = is_valid_epoch_ts(ts);
    bool now_is_epoch = is_valid_epoch_ts(now_epoch);
    uint32_t now_secs = now_is_epoch ? now_epoch : static_cast<uint32_t>(millis() / 1000U);
    if (ts_is_epoch && !now_is_epoch) {
        ts_is_epoch = false;
    }
    if (now_secs < ts) {
        now_secs = ts;
    }
    uint32_t diff = now_secs - ts;

    if (diff >= kSecondsPerYear) {
        uint32_t years = diff / kSecondsPerYear;
        if (years == 0) years = 1;
        snprintf(out, out_len, "%uy ago", static_cast<unsigned>(years));
        return;
    }
    if (diff >= kSecondsPerMonth) {
        uint32_t months = diff / kSecondsPerMonth;
        if (months == 0) months = 1;
        snprintf(out, out_len, "%um ago", static_cast<unsigned>(months));
        return;
    }
    if (diff >= kSecondsPerDay) {
        uint32_t days = diff / kSecondsPerDay;
        if (days == 0) days = 1;
        snprintf(out, out_len, "%ud ago", static_cast<unsigned>(days));
        return;
    }
    if (ts_is_epoch) {
        time_t t = ui_apply_timezone_offset(static_cast<time_t>(ts));
        struct tm* info = gmtime(&t);
        if (info) {
            strftime(out, out_len, "%H:%M:%S", info);
        } else {
            snprintf(out, out_len, "--");
        }
        return;
    }
    format_hms(out, out_len, ts % kSecondsPerDay);
}

ChatConversationScreen::ChatConversationScreen(lv_obj_t* parent, chat::ConversationId conv)
    : conv_(conv)
{
    lv_obj_t* active = lv_screen_active();
    if (!active) {
        Serial.printf("[ChatConversation] WARNING: lv_screen_active() is null\n");
    } else {
        Serial.printf("[ChatConversation] init: active=%p parent=%p\n", active, parent);
    }

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

    if (container_ && !lv_obj_is_valid(container_)) {
        Serial.printf("[ChatConversation] WARNING: container invalid\n");
    }
    if (msg_list_ && !lv_obj_is_valid(msg_list_)) {
        Serial.printf("[ChatConversation] WARNING: msg_list invalid\n");
    }

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

    // ----- Layout: row + bubble + time + text + status -----
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

    item.time_label = chat::ui::layout::create_bubble_time(bubble);
    chat::ui::conversation::styles::apply_bubble_time(item.time_label);
    char time_buf[16];
    format_message_time(time_buf, sizeof(time_buf), msg.timestamp);
    if (conv_.peer == 0) {
        std::string sender;
        if (msg.from == 0) {
            sender = app::AppContext::getInstance().getConfig().short_name;
        } else {
            sender = app::AppContext::getInstance().getContactService().getContactName(msg.from);
            if (sender.empty()) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%04lX",
                         static_cast<unsigned long>(msg.from & 0xFFFF));
                sender = buf;
            }
        }
        std::string line = sender + " " + time_buf;
        lv_label_set_text(item.time_label, line.c_str());
    } else {
        lv_label_set_text(item.time_label, time_buf);
    }

    item.text_label = chat::ui::layout::create_bubble_text(bubble);
    lv_label_set_text(item.text_label, msg.text.c_str());
    chat::ui::conversation::styles::apply_bubble_text(item.text_label);

    item.status_label = chat::ui::layout::create_bubble_status(bubble);
    chat::ui::conversation::styles::apply_bubble_status(item.status_label);
    if (msg.status == MessageStatus::Failed)
    {
        lv_label_set_text(item.status_label, "Failed");
        lv_obj_clear_flag(item.status_label, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_label_set_text(item.status_label, "");
        lv_obj_add_flag(item.status_label, LV_OBJ_FLAG_HIDDEN);
    }

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

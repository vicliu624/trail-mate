#if !defined(ARDUINO_T_WATCH_S3)
/**
 * @file chat_conversation.cpp
 * @brief Chat conversation screen implementation
 */
#include "chat_conversation_components.h"

#include "../../../app/app_context.h"
#include "../ui_common.h"
#include "chat_conversation_input.h"
#include "chat_conversation_layout.h"
#include "chat_conversation_styles.h"

#include "../../../team/protocol/team_location_marker.h"

#include <Arduino.h>
#include <algorithm>
#include <cstdio>
#include <ctime>

extern "C"
{
    extern const lv_image_dsc_t AreaCleared;
    extern const lv_image_dsc_t BaseCamp;
    extern const lv_image_dsc_t GoodFind;
    extern const lv_image_dsc_t rally;
    extern const lv_image_dsc_t sos;
}

namespace chat
{
namespace ui
{

// Keep original constant meaning (bubble max width and 70% rule)
namespace
{
constexpr lv_coord_t kBubbleMaxWidth = 322; // same as original
constexpr lv_coord_t kTeamLocationIconSize = 24;
constexpr uint32_t kSecondsPerDay = 24U * 60U * 60U;
constexpr uint32_t kSecondsPerMonth = 30U * kSecondsPerDay;
constexpr uint32_t kSecondsPerYear = 365U * kSecondsPerDay;
constexpr uint32_t kMinValidEpochSeconds = 1577836800U; // 2020-01-01
} // namespace

static const lv_image_dsc_t* team_location_icon_src(uint8_t icon_id)
{
    switch (static_cast<team::proto::TeamLocationMarkerIcon>(icon_id))
    {
    case team::proto::TeamLocationMarkerIcon::AreaCleared:
        return &AreaCleared;
    case team::proto::TeamLocationMarkerIcon::BaseCamp:
        return &BaseCamp;
    case team::proto::TeamLocationMarkerIcon::GoodFind:
        return &GoodFind;
    case team::proto::TeamLocationMarkerIcon::Rally:
        return &rally;
    case team::proto::TeamLocationMarkerIcon::Sos:
        return &sos;
    default:
        return nullptr;
    }
}

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
    if (ts == 0)
    {
        snprintf(out, out_len, "--");
        return;
    }

    uint32_t now_epoch = static_cast<uint32_t>(time(nullptr));
    bool ts_is_epoch = is_valid_epoch_ts(ts);
    bool now_is_epoch = is_valid_epoch_ts(now_epoch);
    uint32_t now_secs = now_is_epoch ? now_epoch : static_cast<uint32_t>(millis() / 1000U);
    if (ts_is_epoch && !now_is_epoch)
    {
        ts_is_epoch = false;
    }
    if (now_secs < ts)
    {
        now_secs = ts;
    }
    uint32_t diff = now_secs - ts;

    if (!ts_is_epoch)
    {
        if (diff < 60U)
        {
            snprintf(out, out_len, "now");
            return;
        }
        if (diff < 3600U)
        {
            snprintf(out, out_len, "%um", static_cast<unsigned>(diff / 60U));
            return;
        }
        if (diff < kSecondsPerDay)
        {
            snprintf(out, out_len, "%uh", static_cast<unsigned>(diff / 3600U));
            return;
        }
        if (diff < kSecondsPerMonth)
        {
            snprintf(out, out_len, "%ud", static_cast<unsigned>(diff / kSecondsPerDay));
            return;
        }
        if (diff < kSecondsPerYear)
        {
            snprintf(out, out_len, "%umo", static_cast<unsigned>(diff / kSecondsPerMonth));
            return;
        }
        snprintf(out, out_len, "%uy", static_cast<unsigned>(diff / kSecondsPerYear));
        return;
    }

    time_t t = ui_apply_timezone_offset(static_cast<time_t>(ts));
    struct tm* info = gmtime(&t);
    if (info)
    {
        strftime(out, out_len, "%H:%M", info);
    }
    else
    {
        snprintf(out, out_len, "--");
    }
}

ChatConversationScreen::ChatConversationScreen(lv_obj_t* parent, chat::ConversationId conv)
    : conv_(conv)
{
    guard_ = new LifetimeGuard();
    guard_->alive = true;
    guard_->pending_async = 0;

    lv_obj_t* active = lv_screen_active();
    if (!active)
    {
        Serial.printf("[ChatConversation] WARNING: lv_screen_active() is null\n");
    }
    else
    {
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
    ::ui::widgets::top_bar_init(top_bar_, container_);
    const char* title = (conv_.peer == 0) ? "Broadcast" : "Direct";
    ::ui::widgets::top_bar_set_title(top_bar_, title);
    ::ui::widgets::top_bar_set_right_text(top_bar_, "");
    ::ui::widgets::top_bar_set_back_callback(top_bar_, handle_back, this);
    if (top_bar_.container)
    {
        lv_obj_move_to_index(top_bar_.container, 0);
    }

    if (container_)
    {
        lv_obj_add_event_cb(container_, on_root_deleted, LV_EVENT_DELETE, this);
    }

    if (container_ && !lv_obj_is_valid(container_))
    {
        Serial.printf("[ChatConversation] WARNING: container invalid\n");
    }
    if (msg_list_ && !lv_obj_is_valid(msg_list_))
    {
        Serial.printf("[ChatConversation] WARNING: msg_list invalid\n");
    }

    // ----- Event (unchanged) -----
    reply_ctx_.screen = this;
    reply_ctx_.intent = ActionIntent::Reply;
    lv_obj_add_event_cb(reply_btn_, action_event_cb, LV_EVENT_CLICKED, &reply_ctx_);

    // ----- Input layer (explicit, v0 no-op) -----
    chat::ui::conversation::input::init(this, &input_binding_);
}

ChatConversationScreen::~ChatConversationScreen()
{
    if (container_ && lv_obj_is_valid(container_))
    {
        lv_obj_del(container_);
    }
    if (guard_)
    {
        guard_->alive = false;
        if (guard_->pending_async == 0)
        {
            delete guard_;
        }
        guard_ = nullptr;
    }
}

void ChatConversationScreen::addMessage(const chat::ChatMessage& msg)
{
    if (!guard_ || !guard_->alive || !msg_list_ || !lv_obj_is_valid(msg_list_))
    {
        return;
    }
    if (messages_.size() >= MAX_DISPLAY_MESSAGES)
    {
        MessageItem& oldest = messages_[0];
        if (oldest.container)
        {
            lv_obj_del(oldest.container);
        }
        messages_.erase(messages_.begin());
    }

    createMessageItem(msg);
    scrollToBottom();
}

void ChatConversationScreen::clearMessages()
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
    for (auto& item : messages_)
    {
        if (item.container)
        {
            lv_obj_del(item.container);
        }
    }
    messages_.clear();
}

void ChatConversationScreen::scrollToBottom()
{
    if (guard_ && guard_->alive && msg_list_)
    {
        lv_obj_scroll_to_y(msg_list_, LV_COORD_MAX, LV_ANIM_OFF);
    }
}

void ChatConversationScreen::setActionCallback(void (*cb)(ActionIntent intent, void*), void* user_data)
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
    action_cb_ = cb;
    action_cb_user_data_ = user_data;
}

void ChatConversationScreen::setHeaderText(const char* title, const char* status)
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
    ::ui::widgets::top_bar_set_title(top_bar_, title);
    if (status != nullptr)
    {
        ::ui::widgets::top_bar_set_right_text(top_bar_, status);
    }
}

void ChatConversationScreen::updateBatteryFromBoard()
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
    ui_update_top_bar_battery(top_bar_);
}

void ChatConversationScreen::setBackCallback(void (*cb)(void*), void* user_data)
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
    back_cb_ = cb;
    back_cb_user_data_ = user_data;
}

void ChatConversationScreen::createMessageItem(const chat::ChatMessage& msg)
{
    if (!guard_ || !guard_->alive || !msg_list_)
    {
        return;
    }
    MessageItem item;
    item.msg = msg;

    // ----- Layout: row + bubble + time + text + status -----
    item.container = chat::ui::layout::create_message_row(msg_list_);
    chat::ui::conversation::styles::apply_message_row(item.container);

    const bool is_self = (msg.from == 0);

    // Compute bubble max width (same logic as original)
    lv_coord_t max_bubble_w = kBubbleMaxWidth;
    lv_coord_t list_w = chat::ui::layout::get_msg_list_content_width(msg_list_);
    if (list_w > 0)
    {
        // original: candidate = (list_w - 2 * kPadX) * 7 / 10
        // kPadX lives in styles; but for behavior parity we replicate formula using known 8px.
        constexpr lv_coord_t kPadX = 8;
        lv_coord_t candidate = (list_w - 2 * kPadX) * 7 / 10;
        if (candidate > 0 && candidate < max_bubble_w)
        {
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
    if (conv_.peer == 0)
    {
        std::string sender;
        if (msg.from == 0)
        {
            sender = app::AppContext::getInstance().getConfig().short_name;
        }
        else
        {
            sender = app::AppContext::getInstance().getContactService().getContactName(msg.from);
            if (sender.empty())
            {
                char buf[16];
                snprintf(buf, sizeof(buf), "%04lX",
                         static_cast<unsigned long>(msg.from & 0xFFFF));
                sender = buf;
            }
        }
        std::string line = sender + " " + time_buf;
        lv_label_set_text(item.time_label, line.c_str());
    }
    else
    {
        lv_label_set_text(item.time_label, time_buf);
    }

    if (team::proto::team_location_marker_icon_is_valid(msg.team_location_icon))
    {
        const lv_image_dsc_t* icon = team_location_icon_src(msg.team_location_icon);
        if (icon)
        {
            lv_obj_t* marker_icon = lv_image_create(bubble);
            lv_image_set_src(marker_icon, icon);
            lv_obj_set_size(marker_icon, kTeamLocationIconSize, kTeamLocationIconSize);
            lv_image_set_inner_align(marker_icon, LV_IMAGE_ALIGN_CONTAIN);
            lv_obj_set_style_pad_bottom(marker_icon, 2, 0);
        }
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
    auto* ctx = static_cast<ActionContext*>(lv_event_get_user_data(e));
    if (!ctx || !ctx->screen)
    {
        return;
    }
    ChatConversationScreen* screen = ctx->screen;
    if (!screen->guard_ || !screen->guard_->alive)
    {
        return;
    }
    screen->schedule_action_async(ctx->intent);
}

void ChatConversationScreen::async_action_cb(void* user_data)
{
    auto* payload = static_cast<ActionPayload*>(user_data);
    if (!payload)
    {
        return;
    }
    LifetimeGuard* guard = payload->guard;
    if (guard && guard->alive && payload->action_cb)
    {
        payload->action_cb(payload->intent, payload->user_data);
    }
    if (guard && guard->pending_async > 0)
    {
        guard->pending_async--;
        if (!guard->alive && guard->pending_async == 0)
        {
            delete guard;
        }
    }
    delete payload;
}

void ChatConversationScreen::on_root_deleted(lv_event_t* e)
{
    auto* screen = static_cast<ChatConversationScreen*>(lv_event_get_user_data(e));
    if (!screen)
    {
        return;
    }
    screen->handle_root_deleted();
}

void ChatConversationScreen::handle_back(void* user_data)
{
    ChatConversationScreen* screen = static_cast<ChatConversationScreen*>(user_data);
    if (!screen || !screen->guard_ || !screen->guard_->alive)
    {
        return;
    }
    screen->schedule_back_async();
}

void ChatConversationScreen::async_back_cb(void* user_data)
{
    auto* payload = static_cast<BackPayload*>(user_data);
    if (!payload)
    {
        return;
    }
    LifetimeGuard* guard = payload->guard;
    if (guard && guard->alive && payload->back_cb)
    {
        payload->back_cb(payload->user_data);
    }
    if (guard && guard->pending_async > 0)
    {
        guard->pending_async--;
        if (!guard->alive && guard->pending_async == 0)
        {
            delete guard;
        }
    }
    delete payload;
}

lv_timer_t* ChatConversationScreen::add_timer(lv_timer_cb_t cb,
                                              uint32_t period_ms,
                                              void* user_data,
                                              TimerDomain domain)
{
    if (!guard_ || !guard_->alive)
    {
        return nullptr;
    }
    lv_timer_t* timer = lv_timer_create(cb, period_ms, user_data);
    if (timer)
    {
        TimerEntry entry;
        entry.timer = timer;
        entry.domain = domain;
        timers_.push_back(entry);
    }
    return timer;
}

void ChatConversationScreen::clear_timers(TimerDomain domain)
{
    if (timers_.empty())
    {
        return;
    }
    for (auto& entry : timers_)
    {
        if (entry.timer && entry.domain == domain)
        {
            lv_timer_del(entry.timer);
            entry.timer = nullptr;
        }
    }
    timers_.erase(
        std::remove_if(timers_.begin(), timers_.end(),
                       [](const TimerEntry& entry)
                       { return entry.timer == nullptr; }),
        timers_.end());
}

void ChatConversationScreen::clear_all_timers()
{
    for (auto& entry : timers_)
    {
        if (entry.timer)
        {
            lv_timer_del(entry.timer);
            entry.timer = nullptr;
        }
    }
    timers_.clear();
}

void ChatConversationScreen::handle_root_deleted()
{
    if ((!guard_ || !guard_->alive) && container_ == nullptr && msg_list_ == nullptr)
    {
        return;
    }

    if (guard_)
    {
        guard_->alive = false;
    }
    action_cb_ = nullptr;
    action_cb_user_data_ = nullptr;
    back_cb_ = nullptr;
    back_cb_user_data_ = nullptr;
    reply_ctx_.screen = nullptr;

    chat::ui::conversation::input::cleanup(&input_binding_);
    clear_all_timers();

    if (top_bar_.back_btn)
    {
        ::ui::widgets::top_bar_set_back_callback(top_bar_, nullptr, nullptr);
    }

    container_ = nullptr;
    msg_list_ = nullptr;
    action_bar_ = nullptr;
    reply_btn_ = nullptr;
    compose_btn_ = nullptr;
}

void ChatConversationScreen::schedule_action_async(ActionIntent intent)
{
    if (!guard_ || !guard_->alive || !action_cb_)
    {
        return;
    }
    auto* payload = new ActionPayload();
    payload->guard = guard_;
    payload->action_cb = action_cb_;
    payload->user_data = action_cb_user_data_;
    payload->intent = intent;
    guard_->pending_async++;
    lv_async_call(async_action_cb, payload);
}

void ChatConversationScreen::schedule_back_async()
{
    if (!guard_ || !guard_->alive || !back_cb_)
    {
        return;
    }
    auto* payload = new BackPayload();
    payload->guard = guard_;
    payload->back_cb = back_cb_;
    payload->user_data = back_cb_user_data_;
    guard_->pending_async++;
    lv_async_call(async_back_cb, payload);
}

} // namespace ui
} // namespace chat

#endif

#if defined(ARDUINO_T_WATCH_S3)

#include "chat_conversation_components_watch.h"

#include "../../ui_theme.h"

namespace
{
constexpr lv_coord_t kActionBarHeight = 36;
constexpr lv_coord_t kActionButtonHeight = 26;
} // namespace

namespace chat::ui
{

ChatConversationScreen::ChatConversationScreen(lv_obj_t* parent, chat::ConversationId conv)
    : conv_(conv)
{
    guard_ = new LifetimeGuard();
    guard_->alive = true;

    container_ = lv_obj_create(parent);
    lv_obj_set_size(container_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(container_, ::ui::theme::page_bg(), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    msg_list_ = lv_obj_create(container_);
    lv_obj_set_size(msg_list_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(msg_list_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(msg_list_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(msg_list_, 0, 0);
    lv_obj_set_style_pad_left(msg_list_, 8, 0);
    lv_obj_set_style_pad_right(msg_list_, 8, 0);
    lv_obj_set_style_pad_top(msg_list_, 6, 0);
    lv_obj_set_style_pad_bottom(msg_list_, kActionBarHeight + 6, 0);
    lv_obj_set_flex_flow(msg_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(msg_list_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(msg_list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(msg_list_, LV_SCROLLBAR_MODE_ACTIVE);

    action_bar_ = lv_obj_create(container_);
    lv_obj_set_size(action_bar_, LV_PCT(100), kActionBarHeight);
    lv_obj_align(action_bar_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(action_bar_, ::ui::theme::surface_alt(), 0);
    lv_obj_set_style_bg_opa(action_bar_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(action_bar_, 0, 0);
    lv_obj_set_style_pad_all(action_bar_, 4, 0);
    lv_obj_clear_flag(action_bar_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(action_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(action_bar_, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    reply_btn_ = lv_btn_create(action_bar_);
    lv_obj_set_size(reply_btn_, 100, kActionButtonHeight);
    lv_obj_set_style_bg_color(reply_btn_, ::ui::theme::accent(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(reply_btn_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(reply_btn_, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(reply_btn_, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(reply_btn_, action_event_cb, LV_EVENT_CLICKED, this);
    lv_obj_t* reply_label = lv_label_create(reply_btn_);
    lv_label_set_text(reply_label, "Reply");
    lv_obj_center(reply_label);
    lv_obj_set_style_text_color(reply_label, ::ui::theme::white(), 0);

    back_btn_ = lv_btn_create(action_bar_);
    lv_obj_set_size(back_btn_, 100, kActionButtonHeight);
    lv_obj_set_style_bg_color(back_btn_, ::ui::theme::surface(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(back_btn_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(back_btn_, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(back_btn_, ::ui::theme::border(), LV_PART_MAIN);
    lv_obj_set_style_radius(back_btn_, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(back_btn_, back_event_cb, LV_EVENT_CLICKED, this);
    lv_obj_t* back_label = lv_label_create(back_btn_);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);
    lv_obj_set_style_text_color(back_label, ::ui::theme::text(), 0);
}

ChatConversationScreen::~ChatConversationScreen()
{
    if (guard_)
    {
        guard_->alive = false;
    }
    if (container_)
    {
        lv_obj_del(container_);
        container_ = nullptr;
    }
    delete guard_;
    guard_ = nullptr;
}

void ChatConversationScreen::addMessage(const chat::ChatMessage& msg)
{
    if (!msg_list_)
    {
        return;
    }

    if (messages_.size() >= MAX_DISPLAY_MESSAGES)
    {
        auto& front = messages_.front();
        if (front.container)
        {
            lv_obj_del(front.container);
        }
        messages_.erase(messages_.begin());
    }

    MessageItem item{};
    item.msg = msg;
    item.container = lv_obj_create(msg_list_);
    lv_obj_set_width(item.container, LV_PCT(100));
    lv_obj_set_style_bg_opa(item.container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(item.container, 0, 0);
    lv_obj_set_style_pad_top(item.container, 2, 0);
    lv_obj_set_style_pad_bottom(item.container, 2, 0);
    lv_obj_set_flex_flow(item.container, LV_FLEX_FLOW_ROW);
    bool is_self = msg.status == chat::MessageStatus::Sent;
    lv_obj_set_flex_align(item.container,
                          is_self ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    item.text_label = lv_label_create(item.container);
    lv_label_set_long_mode(item.text_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(item.text_label, LV_PCT(80));
    lv_label_set_text(item.text_label, msg.text.c_str());
    lv_obj_set_style_text_color(item.text_label, ::ui::theme::text(), 0);
    lv_obj_set_style_bg_color(item.text_label,
                              is_self ? ::ui::theme::surface_alt() : ::ui::theme::surface(),
                              0);
    lv_obj_set_style_bg_opa(item.text_label, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(item.text_label, 6, 0);
    lv_obj_set_style_radius(item.text_label, 6, 0);

    messages_.push_back(item);
}

void ChatConversationScreen::clearMessages()
{
    if (msg_list_)
    {
        lv_obj_clean(msg_list_);
    }
    messages_.clear();
}

void ChatConversationScreen::scrollToBottom()
{
    if (messages_.empty())
    {
        return;
    }
    lv_obj_scroll_to_view(messages_.back().container, LV_ANIM_OFF);
}

void ChatConversationScreen::setActionCallback(void (*cb)(ActionIntent intent, void*), void* user_data)
{
    action_cb_ = cb;
    action_cb_user_data_ = user_data;
}

void ChatConversationScreen::setHeaderText(const char* title, const char*)
{
    (void)title;
}

void ChatConversationScreen::updateBatteryFromBoard()
{
}

void ChatConversationScreen::setBackCallback(void (*cb)(void*), void* user_data)
{
    back_cb_ = cb;
    back_cb_user_data_ = user_data;
}

void ChatConversationScreen::schedule_action_async(ActionIntent intent)
{
    if (!action_cb_)
    {
        return;
    }
    auto* payload = new ActionPayload();
    payload->guard = guard_;
    payload->action_cb = action_cb_;
    payload->user_data = action_cb_user_data_;
    payload->intent = intent;
    lv_async_call(async_action_cb, payload);
}

void ChatConversationScreen::schedule_back_async()
{
    if (!back_cb_)
    {
        return;
    }
    auto* payload = new BackPayload();
    payload->guard = guard_;
    payload->back_cb = back_cb_;
    payload->user_data = back_cb_user_data_;
    lv_async_call(async_back_cb, payload);
}

void ChatConversationScreen::action_event_cb(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }
    auto* screen = static_cast<ChatConversationScreen*>(lv_event_get_user_data(e));
    if (!screen)
    {
        return;
    }
    screen->schedule_action_async(ActionIntent::Reply);
}

void ChatConversationScreen::back_event_cb(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }
    auto* screen = static_cast<ChatConversationScreen*>(lv_event_get_user_data(e));
    if (!screen)
    {
        return;
    }
    screen->schedule_back_async();
}

void ChatConversationScreen::async_action_cb(void* user_data)
{
    auto* payload = static_cast<ActionPayload*>(user_data);
    if (!payload)
    {
        return;
    }
    if (payload->guard && payload->guard->alive && payload->action_cb)
    {
        payload->action_cb(payload->intent, payload->user_data);
    }
    delete payload;
}

void ChatConversationScreen::async_back_cb(void* user_data)
{
    auto* payload = static_cast<BackPayload*>(user_data);
    if (!payload)
    {
        return;
    }
    if (payload->guard && payload->guard->alive && payload->back_cb)
    {
        payload->back_cb(payload->user_data);
    }
    delete payload;
}

void ChatConversationScreen::handle_back(void* user_data)
{
    auto* screen = static_cast<ChatConversationScreen*>(user_data);
    if (!screen)
    {
        return;
    }
    screen->schedule_back_async();
}

} // namespace chat::ui

#endif

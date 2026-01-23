#include "chat_compose_components.h"

#include "chat_compose_input.h"
#include "chat_compose_layout.h"
#include "chat_compose_styles.h"

#include "../../widgets/ime/ime_widget.h"
#include "../../../chat/usecase/chat_service.h"

#include <Arduino.h>
#include <algorithm>
#include <cstdio> // snprintf
#include <cstring>
#include <vector>

#ifndef CHAT_COMPOSE_LOG_ENABLE
#define CHAT_COMPOSE_LOG_ENABLE 0
#endif

#if CHAT_COMPOSE_LOG_ENABLE
#define CHAT_COMPOSE_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define CHAT_COMPOSE_LOG(...)
#endif

namespace chat::ui
{

static constexpr size_t kMaxInputBytes = 233;
static constexpr uint32_t kSendTimeoutMs = 3000;
static constexpr uint32_t kSendResultHoldMs = 600;

enum class SendState
{
    Idle,
    Waiting,
    DoneOk,
    DoneFail,
    DoneTimeout
};

struct ChatComposeScreen::LifetimeGuard
{
    bool alive = false;
    int pending_async = 0;
    ChatComposeScreen* owner = nullptr;
};

struct ChatComposeScreen::DonePayload
{
    LifetimeGuard* guard = nullptr;
    void (*done_cb)(bool ok, bool timeout, void*) = nullptr;
    void* user_data = nullptr;
    bool ok = false;
    bool timeout = false;
};

struct ChatComposeScreen::Impl
{
    chat::ui::compose::layout::Spec spec;
    chat::ui::compose::layout::Widgets w;
    chat::ui::compose::input::State input_state;
    LifetimeGuard* guard = nullptr;
    std::vector<lv_timer_t*> timers;
    struct ActionContext
    {
        ChatComposeScreen* screen = nullptr;
        ActionIntent intent = ActionIntent::Send;
    };
    ActionContext send_ctx;
    ActionContext cancel_ctx;
    lv_obj_t* sending_overlay = nullptr;
    lv_obj_t* sending_label = nullptr;
    lv_timer_t* send_timer = nullptr;
    uint32_t send_start_ms = 0;
    uint32_t send_finish_ms = 0;
    chat::MessageId pending_msg_id = 0;
    chat::ChatService* send_service = nullptr;
    SendState send_state = SendState::Idle;
    void (*send_done_cb)(bool ok, bool timeout, void*) = nullptr;
    void* send_done_user_data = nullptr;
};

static void set_btn_label_white(lv_obj_t* btn)
{
    lv_obj_t* child = lv_obj_get_child(btn, 0);
    if (child && lv_obj_check_type(child, &lv_label_class))
    {
        lv_obj_set_style_text_color(child, lv_color_white(), 0);
    }
}

lv_timer_t* ChatComposeScreen::add_timer(ChatComposeScreen::Impl* impl,
                                         lv_timer_cb_t cb,
                                         uint32_t period_ms,
                                         void* user_data)
{
    if (!impl) return nullptr;
    lv_timer_t* timer = lv_timer_create(cb, period_ms, user_data);
    if (timer)
    {
        impl->timers.push_back(timer);
    }
    return timer;
}

void ChatComposeScreen::clear_timers(ChatComposeScreen::Impl* impl)
{
    if (!impl) return;
    for (auto* timer : impl->timers)
    {
        if (timer)
        {
            lv_timer_del(timer);
        }
    }
    impl->timers.clear();
    impl->send_timer = nullptr;
}

void ChatComposeScreen::async_done_cb(void* user_data)
{
    auto* payload = static_cast<DonePayload*>(user_data);
    if (!payload)
    {
        return;
    }
    LifetimeGuard* guard = payload->guard;
    if (guard && guard->alive && payload->done_cb)
    {
        payload->done_cb(payload->ok, payload->timeout, payload->user_data);
    }
    if (guard)
    {
        guard->pending_async = std::max(0, guard->pending_async - 1);
        if (!guard->alive && guard->pending_async == 0)
        {
            delete guard;
        }
    }
    delete payload;
}

void ChatComposeScreen::schedule_done_async(LifetimeGuard* guard,
                                            void (*done_cb)(bool ok, bool timeout, void*),
                                            void* user_data,
                                            bool ok,
                                            bool timeout)
{
    if (!guard || !done_cb)
    {
        return;
    }
    auto* payload = new DonePayload();
    payload->guard = guard;
    payload->done_cb = done_cb;
    payload->user_data = user_data;
    payload->ok = ok;
    payload->timeout = timeout;
    guard->pending_async++;
    lv_async_call(async_done_cb, payload);
}

void ChatComposeScreen::on_root_deleted(lv_event_t* e)
{
    auto* screen = static_cast<ChatComposeScreen*>(lv_event_get_user_data(e));
    if (!screen || !screen->impl_)
    {
        return;
    }
    ChatComposeScreen::Impl* impl = screen->impl_;
    if (impl->guard)
    {
        impl->guard->alive = false;
    }
    clear_timers(impl);
    impl->send_service = nullptr;
    impl->send_done_cb = nullptr;
    impl->send_done_user_data = nullptr;
    screen->action_cb_ = nullptr;
    screen->action_cb_user_data_ = nullptr;
    screen->back_cb_ = nullptr;
    screen->back_cb_user_data_ = nullptr;
    screen->ime_widget_ = nullptr;

    LifetimeGuard* guard = impl->guard;
    screen->impl_ = nullptr;
    delete impl;
    if (guard && guard->pending_async == 0)
    {
        delete guard;
    }
}

ChatComposeScreen::ChatComposeScreen(lv_obj_t* parent, chat::ConversationId conv)
    : conv_(conv)
{
    lv_obj_t* active = lv_screen_active();
    if (!active) {
        CHAT_COMPOSE_LOG("[ChatCompose] WARNING: lv_screen_active() is null\n");
    } else {
        CHAT_COMPOSE_LOG("[ChatCompose] init: active=%p parent=%p\n", active, parent);
    }

    impl_ = new Impl();
    impl_->guard = new LifetimeGuard();
    impl_->guard->alive = true;
    impl_->guard->pending_async = 0;
    impl_->guard->owner = this;

    using namespace chat::ui::compose;

    layout::create(parent, impl_->spec, impl_->w);
    styles::apply_all(impl_->w);

    if (impl_->w.container)
    {
        lv_obj_add_event_cb(impl_->w.container, on_root_deleted, LV_EVENT_DELETE, this);
    }

    lv_textarea_set_placeholder_text(impl_->w.textarea, "");
    lv_textarea_set_one_line(impl_->w.textarea, false);
    lv_textarea_set_max_length(impl_->w.textarea, kMaxInputBytes);

    impl_->send_ctx.screen = this;
    impl_->send_ctx.intent = ActionIntent::Send;
    impl_->cancel_ctx.screen = this;
    impl_->cancel_ctx.intent = ActionIntent::Cancel;
    lv_obj_add_event_cb(impl_->w.send_btn, on_action_click, LV_EVENT_CLICKED, &impl_->send_ctx);
    lv_obj_add_event_cb(impl_->w.cancel_btn, on_action_click, LV_EVENT_CLICKED, &impl_->cancel_ctx);

    set_btn_label_white(impl_->w.send_btn);
    set_btn_label_white(impl_->w.cancel_btn);

    init_topbar();

    input::bind_textarea_events(impl_->w, this, on_key, on_text_changed);
    input::setup_default_group_focus(impl_->w);

    if (impl_->w.container && !lv_obj_is_valid(impl_->w.container)) {
        CHAT_COMPOSE_LOG("[ChatCompose] WARNING: container invalid\n");
    }
    if (impl_->w.textarea && !lv_obj_is_valid(impl_->w.textarea)) {
        CHAT_COMPOSE_LOG("[ChatCompose] WARNING: textarea invalid\n");
    }

    refresh_len();
}

ChatComposeScreen::~ChatComposeScreen()
{
    if (!impl_) return;
    if (impl_->w.container && lv_obj_is_valid(impl_->w.container))
    {
        lv_obj_del(impl_->w.container);
    }
}

lv_obj_t* ChatComposeScreen::getObj() const
{
    return impl_ ? impl_->w.container : nullptr;
}

void ChatComposeScreen::init_topbar()
{
    char title_buf[32];

    if (conv_.peer == 0)
    {
        snprintf(title_buf, sizeof(title_buf), "Broadcast");
    }
    else
    {
        snprintf(title_buf, sizeof(title_buf), "%04lX",
                 static_cast<unsigned long>(conv_.peer & 0xFFFF));
    }

    ::ui::widgets::top_bar_set_title(impl_->w.top_bar, title_buf);
    ::ui::widgets::top_bar_set_right_text(impl_->w.top_bar, "RSSI --");
    ::ui::widgets::top_bar_set_back_callback(impl_->w.top_bar, on_back, this);
}

void ChatComposeScreen::setHeaderText(const char* title, const char* status)
{
    if (!impl_) return;
    if (title) ::ui::widgets::top_bar_set_title(impl_->w.top_bar, title);
    if (status) ::ui::widgets::top_bar_set_right_text(impl_->w.top_bar, status);
}

std::string ChatComposeScreen::getText() const
{
    if (!impl_ || !impl_->w.textarea) return "";
    const char* text = lv_textarea_get_text(impl_->w.textarea);
    return text ? std::string(text) : "";
}

void ChatComposeScreen::clearText()
{
    if (!impl_) return;
    lv_textarea_set_text(impl_->w.textarea, "");
    refresh_len();
}

void ChatComposeScreen::beginSend(chat::ChatService* service,
                                  chat::MessageId msg_id,
                                  void (*done_cb)(bool ok, bool timeout, void*),
                                  void* user_data)
{
    if (!impl_ || !impl_->guard || !impl_->guard->alive) return;

    impl_->send_service = service;
    impl_->pending_msg_id = msg_id;
    impl_->send_start_ms = millis();
    impl_->send_finish_ms = 0;
    impl_->send_state = SendState::Waiting;
    impl_->send_done_cb = done_cb;
    impl_->send_done_user_data = user_data;

    if (!impl_->sending_overlay)
    {
        impl_->sending_overlay = lv_obj_create(impl_->w.container);
        lv_obj_set_size(impl_->sending_overlay, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(impl_->sending_overlay, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(impl_->sending_overlay, LV_OPA_60, LV_PART_MAIN);
        lv_obj_set_style_border_width(impl_->sending_overlay, 0, LV_PART_MAIN);
        lv_obj_clear_flag(impl_->sending_overlay, LV_OBJ_FLAG_SCROLLABLE);

        impl_->sending_label = lv_label_create(impl_->sending_overlay);
        lv_obj_set_style_text_color(impl_->sending_label, lv_color_white(), 0);
        lv_obj_center(impl_->sending_label);
    }
    setSendingText("Sending...");
    lv_obj_clear_flag(impl_->sending_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(impl_->sending_overlay);

    if (impl_->w.send_btn) lv_obj_add_state(impl_->w.send_btn, LV_STATE_DISABLED);
    if (impl_->w.cancel_btn) lv_obj_add_state(impl_->w.cancel_btn, LV_STATE_DISABLED);
    if (impl_->w.textarea) lv_obj_add_state(impl_->w.textarea, LV_STATE_DISABLED);
    if (impl_->w.top_bar.back_btn) lv_obj_add_state(impl_->w.top_bar.back_btn, LV_STATE_DISABLED);

    if (impl_->send_timer)
    {
        clear_timers(impl_);
    }
    impl_->send_timer = add_timer(impl_, on_send_timer, 150, this);
    if (!impl_->send_timer) return;

    if (impl_->pending_msg_id == 0 || !impl_->send_service)
    {
        finishSend(false, false, "Send failed");
    }
}

void ChatComposeScreen::finishSend(bool ok, bool timeout, const char* message)
{
    if (!impl_ || !impl_->guard || !impl_->guard->alive) return;
    if (timeout)
    {
        impl_->send_state = SendState::DoneTimeout;
    }
    else if (ok)
    {
        impl_->send_state = SendState::DoneOk;
    }
    else
    {
        impl_->send_state = SendState::DoneFail;
    }
    impl_->send_finish_ms = millis();
    setSendingText(message);
}

void ChatComposeScreen::setSendingText(const char* text)
{
    if (!impl_ || !impl_->sending_label) return;
    lv_label_set_text(impl_->sending_label, text ? text : "");
    lv_obj_center(impl_->sending_label);
}

void ChatComposeScreen::setActionCallback(void (*cb)(ActionIntent intent, void*), void* user_data)
{
    action_cb_ = cb;
    action_cb_user_data_ = user_data;
}

void ChatComposeScreen::setBackCallback(void (*cb)(void*), void* user_data)
{
    back_cb_ = cb;
    back_cb_user_data_ = user_data;
}

void ChatComposeScreen::attachImeWidget(::ui::widgets::ImeWidget* widget)
{
    ime_widget_ = widget;
}

lv_obj_t* ChatComposeScreen::getTextarea() const
{
    return impl_ ? impl_->w.textarea : nullptr;
}

lv_obj_t* ChatComposeScreen::getContent() const
{
    return impl_ ? impl_->w.content : nullptr;
}

lv_obj_t* ChatComposeScreen::getActionBar() const
{
    return impl_ ? impl_->w.action_bar : nullptr;
}

void ChatComposeScreen::refresh_len()
{
    if (!impl_) return;

    const char* text = lv_textarea_get_text(impl_->w.textarea);
    size_t len = text ? strlen(text) : 0;
    size_t remaining = (len < kMaxInputBytes) ? (kMaxInputBytes - len) : 0;

    char buf[16];
    snprintf(buf, sizeof(buf), "Remain: %u", static_cast<unsigned int>(remaining));
    lv_label_set_text(impl_->w.len_label, buf);
}

// ---------- LVGL callbacks ----------

void ChatComposeScreen::on_action_click(lv_event_t* e)
{
    auto* ctx = static_cast<Impl::ActionContext*>(lv_event_get_user_data(e));
    if (!ctx || !ctx->screen)
    {
        return;
    }
    auto* screen = ctx->screen;
    if (!screen->impl_ || !screen->impl_->guard || !screen->impl_->guard->alive)
    {
        return;
    }
    if (!screen->action_cb_)
    {
        return;
    }
    screen->action_cb_(ctx->intent, screen->action_cb_user_data_);
}

void ChatComposeScreen::on_text_changed(lv_event_t* e)
{
    auto* screen = static_cast<ChatComposeScreen*>(lv_event_get_user_data(e));
    if (!screen || !screen->impl_ || !screen->impl_->guard || !screen->impl_->guard->alive)
    {
        return;
    }
    screen->refresh_len();
}

void ChatComposeScreen::on_back(void* user_data)
{
    auto* screen = static_cast<ChatComposeScreen*>(user_data);
    if (!screen || !screen->impl_ || !screen->impl_->guard || !screen->impl_->guard->alive)
    {
        return;
    }
    if (screen->back_cb_)
    {
        screen->back_cb_(screen->back_cb_user_data_);
    }
}

void ChatComposeScreen::on_key(lv_event_t* e)
{
    auto* screen = static_cast<ChatComposeScreen*>(lv_event_get_user_data(e));
    if (!screen || !screen->impl_ || !screen->impl_->guard || !screen->impl_->guard->alive)
    {
        return;
    }

    if (screen->ime_widget_ && screen->ime_widget_->handle_key(e))
    {
        return;
    }

    uint32_t key = lv_event_get_key(e);
    CHAT_COMPOSE_LOG("[ChatCompose] key=%lu\n", static_cast<unsigned long>(key));

    lv_indev_t* indev = lv_indev_get_act();
    bool is_encoder = indev && lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER;

    if (is_encoder && key == LV_KEY_ENTER && screen->impl_->w.send_btn)
    {
        if (lv_group_t* g = lv_group_get_default())
        {
            lv_group_focus_obj(screen->impl_->w.send_btn);
        }
    }
}

void ChatComposeScreen::on_send_timer(lv_timer_t* timer)
{
    auto* screen = static_cast<ChatComposeScreen*>(timer->user_data);
    if (!screen || !screen->impl_ || !screen->impl_->guard || !screen->impl_->guard->alive)
    {
        return;
    }
    auto* impl = screen->impl_;

    uint32_t now = millis();
    if (impl->send_state == SendState::Waiting)
    {
        if (!impl->send_service || impl->pending_msg_id == 0)
        {
            screen->finishSend(false, false, "Send failed");
        }
        else
        {
            const ChatMessage* msg = impl->send_service->getMessage(impl->pending_msg_id);
            if (msg)
            {
                if (msg->status == MessageStatus::Sent)
                {
                    screen->finishSend(true, false, "Sent");
                }
                else if (msg->status == MessageStatus::Failed)
                {
                    screen->finishSend(false, false, "Failed");
                }
            }
        }
        if (impl->send_state == SendState::Waiting &&
            (now - impl->send_start_ms >= kSendTimeoutMs))
        {
            screen->finishSend(false, true, "No response");
        }
    }

    if (impl->send_state != SendState::Waiting &&
        impl->send_state != SendState::Idle &&
        (now - impl->send_finish_ms >= kSendResultHoldMs))
    {
        auto* done_cb = impl->send_done_cb;
        void* done_user = impl->send_done_user_data;
        bool ok = (impl->send_state == SendState::DoneOk);
        bool timeout = (impl->send_state == SendState::DoneTimeout);

        if (impl->sending_overlay)
        {
            lv_obj_add_flag(impl->sending_overlay, LV_OBJ_FLAG_HIDDEN);
        }
        clear_timers(impl);
        impl->send_done_cb = nullptr;
        impl->send_done_user_data = nullptr;
        impl->send_service = nullptr;
        impl->send_state = SendState::Idle;
        schedule_done_async(impl->guard, done_cb, done_user, ok, timeout);
    }
}

} // namespace chat::ui

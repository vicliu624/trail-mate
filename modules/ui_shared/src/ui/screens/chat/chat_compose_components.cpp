#if !defined(ARDUINO_T_WATCH_S3)
#include "ui/screens/chat/chat_compose_components.h"

#include "ui/screens/chat/chat_compose_input.h"
#include "ui/screens/chat/chat_compose_layout.h"
#include "ui/screens/chat/chat_compose_styles.h"

#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/ui_common.h"
#include "ui/widgets/ime/ime_widget.h"
#include "ui/widgets/text_candidate_picker.h"

#include <cstdint>
#include <cstdio> // snprintf
#include <cstring>

#ifndef CHAT_COMPOSE_LOG_ENABLE
#define CHAT_COMPOSE_LOG_ENABLE 0
#endif

#if CHAT_COMPOSE_LOG_ENABLE
#define CHAT_COMPOSE_LOG(...) std::printf(__VA_ARGS__)
#else
#define CHAT_COMPOSE_LOG(...)
#endif

namespace chat::ui
{

static constexpr size_t kMaxInputBytes = 233;

struct ChatComposeScreen::LifetimeGuard
{
    bool alive = false;
    int pending_async = 0;
};

struct ChatComposeScreen::Impl
{
    chat::ui::compose::layout::Spec spec;
    chat::ui::compose::layout::Widgets w;
    chat::ui::compose::input::State input_state;
    LifetimeGuard* guard = nullptr;
    struct ActionContext
    {
        ChatComposeScreen* screen = nullptr;
        ActionIntent intent = ActionIntent::Send;
    };
    ActionContext send_ctx;
    ActionContext auxiliary_ctx;
    ActionContext cancel_ctx;
    lv_obj_t* sym_btn = nullptr;
    lv_obj_t* emoji_btn = nullptr;
};

static void set_btn_label_white(lv_obj_t* btn)
{
    lv_obj_t* child = lv_obj_get_child(btn, 0);
    if (child && lv_obj_check_type(child, &lv_label_class))
    {
        lv_obj_set_style_text_color(child, lv_color_hex(0x3A2A1A), 0);
    }
}

static void set_btn_label_text(lv_obj_t* btn, const char* text)
{
    if (!btn || !text) return;
    lv_obj_t* child = lv_obj_get_child(btn, 0);
    if (child && lv_obj_check_type(child, &lv_label_class))
    {
        ::ui::i18n::set_label_text(child, text);
    }
}

static void fit_btn_to_label(lv_obj_t* btn, int pad_lr)
{
    if (!btn) return;
    lv_obj_t* child = lv_obj_get_child(btn, 0);
    if (!child || !lv_obj_check_type(child, &lv_label_class)) return;
    lv_obj_update_layout(child);
    int label_w = lv_obj_get_width(child);
    if (label_w <= 0) return;
    int target_w = label_w + pad_lr * 2;
    int cur_w = lv_obj_get_width(btn);
    if (target_w > cur_w)
    {
        lv_obj_set_width(btn, target_w);
    }
}

static void refresh_textarea_content_font(lv_obj_t* textarea)
{
    if (!textarea)
    {
        return;
    }

    const char* text = lv_textarea_get_text(textarea);
    ::ui::fonts::apply_content_font(textarea, text ? text : "", ::ui::fonts::ui_chrome_font());
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
    if (!active)
    {
        CHAT_COMPOSE_LOG("[ChatCompose] WARNING: lv_screen_active() is null\n");
    }
    else
    {
        CHAT_COMPOSE_LOG("[ChatCompose] init: active=%p parent=%p\n", active, parent);
    }

    impl_ = new Impl();
    impl_->guard = new LifetimeGuard();
    impl_->guard->alive = true;

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
    refresh_textarea_content_font(impl_->w.textarea);

    impl_->send_ctx.screen = this;
    impl_->send_ctx.intent = ActionIntent::Send;
    impl_->auxiliary_ctx.screen = this;
    impl_->auxiliary_ctx.intent = ActionIntent::Position;
    impl_->cancel_ctx.screen = this;
    impl_->cancel_ctx.intent = ActionIntent::Cancel;
    lv_obj_add_event_cb(impl_->w.send_btn, on_action_click, LV_EVENT_CLICKED, &impl_->send_ctx);
    lv_obj_add_event_cb(impl_->w.position_btn, on_action_click, LV_EVENT_CLICKED, &impl_->auxiliary_ctx);
    // Voice mode deliberately uses press/release rather than CLICKED. CLICKED
    // is emitted only after release, which would start a five-second capture
    // after the user had already stopped holding the control.
    lv_obj_add_event_cb(impl_->w.position_btn,
                        on_voice_pressed,
                        LV_EVENT_PRESSED,
                        &impl_->auxiliary_ctx);
    lv_obj_add_event_cb(impl_->w.position_btn,
                        on_voice_released,
                        LV_EVENT_RELEASED,
                        &impl_->auxiliary_ctx);
    lv_obj_add_event_cb(impl_->w.position_btn,
                        on_voice_released,
                        LV_EVENT_PRESS_LOST,
                        &impl_->auxiliary_ctx);
    lv_obj_add_event_cb(impl_->w.cancel_btn, on_action_click, LV_EVENT_CLICKED, &impl_->cancel_ctx);
    lv_obj_add_event_cb(impl_->w.send_btn, on_key, LV_EVENT_KEY, this);
    lv_obj_add_event_cb(impl_->w.position_btn, on_key, LV_EVENT_KEY, this);
    lv_obj_add_event_cb(impl_->w.cancel_btn, on_key, LV_EVENT_KEY, this);

    set_btn_label_white(impl_->w.send_btn);
    set_btn_label_white(impl_->w.position_btn);
    set_btn_label_white(impl_->w.cancel_btn);
    if (impl_->w.position_btn)
    {
        lv_obj_add_flag(impl_->w.position_btn, LV_OBJ_FLAG_HIDDEN);
    }

    init_topbar();

    input::bind_textarea_events(impl_->w, this, on_key, on_text_changed);
    input::setup_default_group_focus(impl_->w);
    syncFocusOrder();

    if (impl_->w.container && !lv_obj_is_valid(impl_->w.container))
    {
        CHAT_COMPOSE_LOG("[ChatCompose] WARNING: container invalid\n");
    }
    if (impl_->w.textarea && !lv_obj_is_valid(impl_->w.textarea))
    {
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
    if (!impl_) return;
    LifetimeGuard* guard = impl_->guard;
    if (guard)
    {
        guard->alive = false;
        if (guard->pending_async == 0)
        {
            delete guard;
        }
    }
    delete impl_;
    impl_ = nullptr;
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
        snprintf(title_buf, sizeof(title_buf), "%s", ::ui::i18n::tr("Broadcast"));
    }
    else
    {
        snprintf(title_buf, sizeof(title_buf), "%04lX",
                 static_cast<unsigned long>(conv_.peer & 0xFFFF));
    }

    ::ui::widgets::top_bar_set_title(impl_->w.top_bar, title_buf);
    ::ui::widgets::top_bar_set_back_callback(impl_->w.top_bar, on_back, this);
}

void ChatComposeScreen::setHeaderText(const char* title, const char* status)
{
    if (!impl_) return;
    if (title) ::ui::widgets::top_bar_set_title(impl_->w.top_bar, title);
    if (status) ::ui::widgets::top_bar_set_right_text(impl_->w.top_bar, status);
}

void ChatComposeScreen::setActionLabels(const char* send_label, const char* cancel_label)
{
    if (!impl_) return;
    if (send_label)
    {
        set_btn_label_text(impl_->w.send_btn, send_label);
        fit_btn_to_label(impl_->w.send_btn, 8);
    }
    if (cancel_label)
    {
        set_btn_label_text(impl_->w.cancel_btn, cancel_label);
        fit_btn_to_label(impl_->w.cancel_btn, 8);
    }
}

void ChatComposeScreen::setPositionButton(const char* label, bool visible)
{
    if (!impl_ || !impl_->w.position_btn) return;
    impl_->auxiliary_ctx.intent = ActionIntent::Position;
    if (label)
    {
        set_btn_label_text(impl_->w.position_btn, label);
        fit_btn_to_label(impl_->w.position_btn, 8);
    }
    if (visible)
    {
        lv_obj_clear_flag(impl_->w.position_btn, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(impl_->w.position_btn, LV_OBJ_FLAG_HIDDEN);
    }

    syncFocusOrder();
}

void ChatComposeScreen::setVoiceButton(const char* label, bool visible)
{
    if (!impl_ || !impl_->w.position_btn) return;
    impl_->auxiliary_ctx.intent = ActionIntent::VoiceStart;
    const bool visibility_changed =
        lv_obj_has_flag(impl_->w.position_btn, LV_OBJ_FLAG_HIDDEN) == visible;
    if (label)
    {
        set_btn_label_text(impl_->w.position_btn, label);
        fit_btn_to_label(impl_->w.position_btn, 8);
    }
    if (visible)
    {
        lv_obj_clear_flag(impl_->w.position_btn, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(impl_->w.position_btn, LV_OBJ_FLAG_HIDDEN);
    }

    // Recording begins on LV_EVENT_PRESSED and ends on LV_EVENT_RELEASED.
    // Rebuilding the LVGL group just to refresh "Voice" -> "Release" removes
    // the currently pressed object from the group, which transfers encoder
    // focus before the release reaches it. Only a visibility transition
    // changes the group membership; a label/timer refresh must preserve the
    // focused Voice button for the entire press-and-hold sequence.
    if (visibility_changed)
    {
        syncFocusOrder();
    }
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
    if (ime_widget_)
    {
        ime_widget_->setText("");
    }
    else
    {
        lv_textarea_set_text(impl_->w.textarea, "");
        refresh_textarea_content_font(impl_->w.textarea);
    }
    refresh_len();
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
    if (!impl_ || !widget || !impl_->w.textarea)
    {
        return;
    }

    lv_obj_t* ime_toggle = widget->toggle_btn();
    lv_obj_t* toolbar = ime_toggle && lv_obj_is_valid(ime_toggle) ? lv_obj_get_parent(ime_toggle) : nullptr;
    if (!toolbar || !lv_obj_is_valid(toolbar))
    {
        impl_->sym_btn = nullptr;
        impl_->emoji_btn = nullptr;
        return;
    }

    // Keep the invisible IME proxy out of the rotary sequence.  Hardware-key
    // input is routed through the visible textarea instead of the mode button:
    // a candidate-confirm key must never also activate the focused IME toggle.
    lv_obj_add_event_cb(ime_toggle, on_key, LV_EVENT_KEY, this);

    lv_group_t* group = lv_group_get_default();
    const auto ensure_candidate_button =
        [&](lv_obj_t*& button, ::ui::widgets::text_candidates::CandidateSet set, std::uint32_t index)
    {
        if (button && (!lv_obj_is_valid(button) || lv_obj_get_parent(button) != toolbar))
        {
            button = nullptr;
        }
        if (!button)
        {
            button = ::ui::widgets::add_text_candidate_button(
                toolbar,
                impl_->w.textarea,
                set,
                group,
                ime_toggle);
        }
        else
        {
            lv_obj_set_user_data(button, impl_->w.textarea);
            if (group)
            {
                lv_group_remove_obj(button);
                lv_group_add_obj(group, button);
            }
        }
        if (button)
        {
            lv_obj_move_to_index(button, index);
        }
    };

    ensure_candidate_button(
        impl_->sym_btn,
        ::ui::widgets::text_candidates::CandidateSet::Symbols,
        1);
    ensure_candidate_button(
        impl_->emoji_btn,
        ::ui::widgets::text_candidates::CandidateSet::Emoji,
        2);
    syncFocusOrder();
    lv_group_focus_obj(impl_->w.textarea);
}

void ChatComposeScreen::syncFocusOrder(bool focus_ime)
{
    if (!impl_)
    {
        return;
    }
    lv_group_t* const group = lv_group_get_default();
    if (!group)
    {
        return;
    }

    // Hardware keyboard commits require the textarea in the focus group on
    // every target. Rotary controls follow it in their visual sequence; Zero
    // puts Send next. The hidden IME proxy stays internal.
    lv_obj_t* const ime_toggle =
        ime_widget_ && lv_obj_is_valid(ime_widget_->toggle_btn())
            ? ime_widget_->toggle_btn()
            : nullptr;
    lv_obj_t* const focus_proxy =
        ime_widget_ && lv_obj_is_valid(ime_widget_->focus_obj())
            ? ime_widget_->focus_obj()
            : nullptr;
    const auto remove = [group](lv_obj_t* object)
    {
        if (object && lv_obj_is_valid(object))
        {
            lv_group_remove_obj(object);
        }
    };
    remove(impl_->w.textarea);
    remove(focus_proxy);
    remove(ime_toggle);
    remove(impl_->sym_btn);
    remove(impl_->emoji_btn);
    remove(impl_->w.send_btn);
    remove(impl_->w.position_btn);
    remove(impl_->w.cancel_btn);
    remove(impl_->w.top_bar.back_btn);

    const auto add_visible = [group](lv_obj_t* object)
    {
        if (object && lv_obj_is_valid(object) &&
            !lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN))
        {
            lv_group_add_obj(group, object);
        }
    };
    add_visible(impl_->w.textarea);
#if defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
    // The hardware keyboard's next stop after typing is the primary action.
    add_visible(impl_->w.send_btn);
#endif
    add_visible(ime_toggle);
    add_visible(impl_->sym_btn);
    add_visible(impl_->emoji_btn);
#if !defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
    add_visible(impl_->w.send_btn);
#endif
    add_visible(impl_->w.position_btn);
    add_visible(impl_->w.cancel_btn);
    add_visible(impl_->w.top_bar.back_btn);

    if (focus_ime && ime_toggle)
    {
        lv_group_focus_obj(ime_toggle);
    }
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

    char buf[32];
    snprintf(buf,
             sizeof(buf),
             "%s",
             ::ui::i18n::format("Remain: %u", static_cast<unsigned int>(remaining)).c_str());
    ::ui::i18n::set_label_text_raw(impl_->w.len_label, buf);
}

// ---------- LVGL callbacks ----------

void ChatComposeScreen::release_async_guard(LifetimeGuard* guard)
{
    if (!guard)
    {
        return;
    }
    if (guard->pending_async > 0)
    {
        guard->pending_async--;
    }
    if (!guard->alive && guard->pending_async == 0)
    {
        delete guard;
    }
}

void ChatComposeScreen::async_action_cb(void* user_data)
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
    release_async_guard(guard);
    delete payload;
}

void ChatComposeScreen::async_back_cb(void* user_data)
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
    release_async_guard(guard);
    delete payload;
}

void ChatComposeScreen::schedule_action_async(ActionIntent intent)
{
    if (!impl_ || !impl_->guard || !impl_->guard->alive || !action_cb_)
    {
        return;
    }
    auto* payload = new ActionPayload();
    payload->guard = impl_->guard;
    payload->action_cb = action_cb_;
    payload->user_data = action_cb_user_data_;
    payload->intent = intent;
    impl_->guard->pending_async++;
    if (lv_async_call(async_action_cb, payload) != LV_RESULT_OK)
    {
        release_async_guard(payload->guard);
        delete payload;
    }
}

void ChatComposeScreen::schedule_back_async()
{
    if (!impl_ || !impl_->guard || !impl_->guard->alive || !back_cb_)
    {
        return;
    }
    auto* payload = new BackPayload();
    payload->guard = impl_->guard;
    payload->back_cb = back_cb_;
    payload->user_data = back_cb_user_data_;
    impl_->guard->pending_async++;
    if (lv_async_call(async_back_cb, payload) != LV_RESULT_OK)
    {
        release_async_guard(payload->guard);
        delete payload;
    }
}

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
    // A voice hold has already begun on LV_EVENT_PRESSED. Ignore the synthetic
    // click emitted after release so it cannot begin a second recording.
    if (ctx->intent == ActionIntent::VoiceStart)
    {
        return;
    }
    screen->schedule_action_async(ctx->intent);
}

void ChatComposeScreen::on_voice_pressed(lv_event_t* e)
{
    auto* ctx = static_cast<Impl::ActionContext*>(lv_event_get_user_data(e));
    if (!ctx || ctx->intent != ActionIntent::VoiceStart || !ctx->screen)
    {
        return;
    }
    auto* const screen = ctx->screen;
    if (!screen->impl_ || !screen->impl_->guard || !screen->impl_->guard->alive ||
        !screen->action_cb_)
    {
        return;
    }
    CHAT_COMPOSE_LOG("[ChatCompose][VMP] voice press: begin capture\n");
    screen->action_cb_(ActionIntent::VoiceStart, screen->action_cb_user_data_);
}

void ChatComposeScreen::on_voice_released(lv_event_t* e)
{
    auto* ctx = static_cast<Impl::ActionContext*>(lv_event_get_user_data(e));
    if (!ctx || ctx->intent != ActionIntent::VoiceStart || !ctx->screen)
    {
        return;
    }
    auto* const screen = ctx->screen;
    if (!screen->impl_ || !screen->impl_->guard || !screen->impl_->guard->alive ||
        !screen->action_cb_)
    {
        return;
    }
    // Stopping capture may immediately switch back to the conversation and
    // destroy this compose button.  Defer that state transition until after
    // LVGL has finished dispatching RELEASED/PRESS_LOST; recording itself was
    // already started synchronously on PRESSED, so this does not reintroduce
    // the old click-to-start behavior.
    CHAT_COMPOSE_LOG("[ChatCompose][VMP] voice release: schedule capture stop\n");
    screen->schedule_action_async(ActionIntent::VoiceStop);
}

void ChatComposeScreen::on_text_changed(lv_event_t* e)
{
    auto* screen = static_cast<ChatComposeScreen*>(lv_event_get_user_data(e));
    if (!screen || !screen->impl_ || !screen->impl_->guard || !screen->impl_->guard->alive)
    {
        return;
    }
    refresh_textarea_content_font(screen->impl_->w.textarea);
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
        screen->schedule_back_async();
    }
}

void ChatComposeScreen::on_key(lv_event_t* e)
{
    auto* screen = static_cast<ChatComposeScreen*>(lv_event_get_user_data(e));
    if (!screen || !screen->impl_ || !screen->impl_->guard || !screen->impl_->guard->alive)
    {
        return;
    }

    uint32_t key = lv_event_get_key(e);
    lv_obj_t* const target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_indev_t* indev = lv_indev_get_act();
    const bool is_encoder = indev && lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER;
    if (key == LV_KEY_ESC)
    {
        if (screen->back_cb_)
        {
            screen->schedule_back_async();
        }
        lv_event_stop_processing(e);
        return;
    }

    if (key == LV_KEY_ENTER && screen->impl_->w.send_btn)
    {
        lv_obj_t* focused = nullptr;
        if (lv_group_t* g = lv_group_get_default())
        {
            focused = lv_group_get_focused(g);
        }
        if (target == screen->impl_->w.send_btn || focused == screen->impl_->w.send_btn)
        {
#if defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
            // Keypad ENTER emits CLICKED on release. Injecting it here as
            // well schedules a second send for the same physical key press.
            return;
#else
            lv_obj_send_event(screen->impl_->w.send_btn, LV_EVENT_CLICKED, nullptr);
            lv_event_stop_processing(e);
            return;
#endif
        }
    }

    // LVGL turns the encoder's ENTER on a focused button into CLICKED.  Do
    // not consume it here: the visible IME control must keep its ordinary
    // mode-switch action.  Keypad ENTER continues into the IME as text input.
    if (screen->ime_widget_ && target == screen->ime_widget_->toggle_btn() &&
        is_encoder && key == LV_KEY_ENTER)
    {
        return;
    }

    if (screen->ime_widget_ && screen->ime_widget_->handle_key(e))
    {
        return;
    }

    CHAT_COMPOSE_LOG("[ChatCompose] key=%lu\n", static_cast<unsigned long>(key));

    if (is_encoder && key == LV_KEY_ENTER && screen->impl_->w.send_btn)
    {
        if (lv_group_get_default())
        {
            lv_group_focus_obj(screen->impl_->w.send_btn);
        }
    }
}

} // namespace chat::ui

#endif

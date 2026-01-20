#include "chat_compose_components.h"

#include "chat_compose_input.h"
#include "chat_compose_layout.h"
#include "chat_compose_styles.h"

#include "../../widgets/ime/ime_widget.h"

#include <Arduino.h>
#include <cstdio> // snprintf
#include <cstring>

namespace chat::ui
{

static constexpr size_t kMaxInputBytes = 233;

struct ChatComposeScreen::Impl
{
    chat::ui::compose::layout::Spec spec;
    chat::ui::compose::layout::Widgets w;
    chat::ui::compose::input::State input_state;
};

static void set_btn_label_white(lv_obj_t* btn)
{
    lv_obj_t* child = lv_obj_get_child(btn, 0);
    if (child && lv_obj_check_type(child, &lv_label_class))
    {
        lv_obj_set_style_text_color(child, lv_color_white(), 0);
    }
}

ChatComposeScreen::ChatComposeScreen(lv_obj_t* parent, chat::ConversationId conv)
    : conv_(conv)
{

    impl_ = new Impl();

    using namespace chat::ui::compose;

    layout::create(parent, impl_->spec, impl_->w);
    styles::apply_all(impl_->w);

    lv_textarea_set_placeholder_text(impl_->w.textarea, "");
    lv_textarea_set_one_line(impl_->w.textarea, false);
    lv_textarea_set_max_length(impl_->w.textarea, kMaxInputBytes);

    lv_obj_add_event_cb(impl_->w.send_btn, on_action_click, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(impl_->w.cancel_btn, on_action_click, LV_EVENT_CLICKED, this);

    set_btn_label_white(impl_->w.send_btn);
    set_btn_label_white(impl_->w.cancel_btn);

    init_topbar();

    input::bind_textarea_events(impl_->w, this, on_key, on_text_changed);
    input::setup_default_group_focus(impl_->w);

    refresh_len();
}

ChatComposeScreen::~ChatComposeScreen()
{
    if (!impl_) return;

    if (impl_->w.container)
    {
        lv_obj_del(impl_->w.container);
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

void ChatComposeScreen::setActionCallback(void (*cb)(bool send, void*), void* user_data)
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
    auto* screen = static_cast<ChatComposeScreen*>(lv_event_get_user_data(e));
    if (!screen || !screen->action_cb_ || !screen->impl_) return;

    auto* target = reinterpret_cast<lv_obj_t*>(lv_event_get_target(e)); // 兼容 void*
    bool send = (target == screen->impl_->w.send_btn);
    screen->action_cb_(send, screen->action_cb_user_data_);
}

void ChatComposeScreen::on_text_changed(lv_event_t* e)
{
    auto* screen = static_cast<ChatComposeScreen*>(lv_event_get_user_data(e));
    if (!screen) return;
    screen->refresh_len();
}

void ChatComposeScreen::on_back(void* user_data)
{
    auto* screen = static_cast<ChatComposeScreen*>(user_data);
    if (screen && screen->back_cb_)
    {
        screen->back_cb_(screen->back_cb_user_data_);
    }
}

void ChatComposeScreen::on_key(lv_event_t* e)
{
    auto* screen = static_cast<ChatComposeScreen*>(lv_event_get_user_data(e));
    if (!screen || !screen->impl_) return;

    if (screen->ime_widget_ && screen->ime_widget_->handle_key(e))
    {
        return;
    }

    uint32_t key = lv_event_get_key(e);
    Serial.printf("[ChatCompose] key=%lu\n", static_cast<unsigned long>(key));

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

} // namespace chat::ui

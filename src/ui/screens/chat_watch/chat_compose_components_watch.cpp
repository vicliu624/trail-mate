#if defined(ARDUINO_T_WATCH_S3)

#include "chat_compose_components_watch.h"

#include "../../../input/morse_engine.h"
#include "../../ui_theme.h"
#include "../../widgets/system_notification.h"
#include <Arduino.h>

namespace
{
constexpr lv_coord_t kButtonHeight = 46;
}

namespace chat::ui
{

ChatComposeScreen::ChatComposeScreen(lv_obj_t* parent, chat::ConversationId conv)
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

    content_ = lv_obj_create(container_);
    lv_obj_set_size(content_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(content_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(content_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_style_pad_left(content_, 16, 0);
    lv_obj_set_style_pad_right(content_, 16, 0);
    lv_obj_set_style_pad_top(content_, 16, 0);
    lv_obj_set_style_pad_bottom(content_, 16, 0);
    lv_obj_set_style_pad_row(content_, 12, 0);
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(content_, LV_OBJ_FLAG_SCROLLABLE);

    quick_texts_ = {"OK", "Yes", "No", "On my way"};

    showMain();
}

ChatComposeScreen::~ChatComposeScreen()
{
    if (guard_)
    {
        guard_->alive = false;
    }
    stopMorse();
    if (container_)
    {
        lv_obj_del(container_);
        container_ = nullptr;
    }
    delete guard_;
    guard_ = nullptr;
}

void ChatComposeScreen::setHeaderText(const char* title, const char*)
{
    (void)title;
}

void ChatComposeScreen::setActionLabels(const char*, const char*)
{
}

void ChatComposeScreen::setPositionButton(const char* label, bool visible)
{
    (void)label;
    (void)visible;
}

std::string ChatComposeScreen::getText() const
{
    return selected_text_;
}

void ChatComposeScreen::clearText()
{
    selected_text_.clear();
}

void ChatComposeScreen::beginSend(chat::ChatService*,
                                  chat::MessageId,
                                  void (*done_cb)(bool ok, bool timeout, void*),
                                  void* user_data)
{
    if (done_cb)
    {
        done_cb(true, false, user_data);
    }
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

void ChatComposeScreen::attachImeWidget(::ui::widgets::ImeWidget*)
{
}

lv_obj_t* ChatComposeScreen::getTextarea() const
{
    return nullptr;
}

lv_obj_t* ChatComposeScreen::getContent() const
{
    return content_;
}

lv_obj_t* ChatComposeScreen::getActionBar() const
{
    return nullptr;
}

lv_obj_t* ChatComposeScreen::getObj() const
{
    return container_;
}

void ChatComposeScreen::showMain()
{
    view_mode_ = ViewMode::Main;
    stopMorse();
    if (!content_)
    {
        return;
    }
    lv_obj_clean(content_);
    lv_obj_clear_flag(content_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);

    auto make_btn = [&](lv_obj_t** btn_out, const char* text)
    {
        lv_obj_t* btn = lv_btn_create(content_);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, kButtonHeight);
        lv_obj_set_style_bg_color(btn, ::ui::theme::surface(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn, ::ui::theme::border(), LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_left(btn, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_right(btn, 12, LV_PART_MAIN);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, main_event_cb, LV_EVENT_CLICKED, this);
        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, text);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_text_color(label, ::ui::theme::text(), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
        if (btn_out) *btn_out = btn;
    };

    make_btn(&mic_btn_, "Mic");
    make_btn(&morse_btn_, "Morse");
    make_btn(&preset_btn_, "Preset");
    make_btn(&back_btn_, "Back");
}

void ChatComposeScreen::showPreset()
{
    view_mode_ = ViewMode::Preset;
    stopMorse();
    if (!content_)
    {
        return;
    }
    lv_obj_clean(content_);
    lv_obj_add_flag(content_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(content_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_ACTIVE);

    for (size_t i = 0; i < quick_texts_.size(); ++i)
    {
        lv_obj_t* btn = lv_btn_create(content_);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, kButtonHeight);
        lv_obj_set_style_bg_color(btn, ::ui::theme::surface(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn, ::ui::theme::border(), LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_left(btn, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_right(btn, 12, LV_PART_MAIN);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, preset_event_cb, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);

        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, quick_texts_[i].c_str());
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_text_color(label, ::ui::theme::text(), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    }

    lv_obj_t* back_btn = lv_btn_create(content_);
    lv_obj_set_width(back_btn, LV_PCT(100));
    lv_obj_set_height(back_btn, kButtonHeight);
    lv_obj_set_style_bg_color(back_btn, ::ui::theme::surface_alt(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(back_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(back_btn, ::ui::theme::border(), LV_PART_MAIN);
    lv_obj_set_style_radius(back_btn, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_left(back_btn, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_right(back_btn, 12, LV_PART_MAIN);
    lv_obj_clear_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(back_btn, preset_event_cb, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(back_btn, (void*)(intptr_t)-1);
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_align(back_label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_text_color(back_label, ::ui::theme::text(), 0);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_18, 0);
}

void ChatComposeScreen::showMorse()
{
    view_mode_ = ViewMode::Morse;
    stopMorse();
    if (!content_)
    {
        return;
    }
    lv_obj_clean(content_);
    lv_obj_clear_flag(content_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_left(content_, 12, 0);
    lv_obj_set_style_pad_right(content_, 12, 0);
    lv_obj_set_style_pad_top(content_, 12, 0);
    lv_obj_set_style_pad_bottom(content_, 12, 0);
    lv_obj_set_style_pad_row(content_, 8, 0);
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    morse_title_label_ = lv_label_create(content_);
    lv_label_set_text(morse_title_label_, "Morse Input");
    lv_obj_set_style_text_color(morse_title_label_, ::ui::theme::text(), 0);
    lv_obj_set_style_text_font(morse_title_label_, &lv_font_montserrat_18, 0);

    morse_status_label_ = lv_label_create(content_);
    lv_label_set_text(morse_status_label_, "Status: CALIB");
    lv_obj_set_style_text_color(morse_status_label_, ::ui::theme::text(), 0);
    lv_obj_set_style_text_font(morse_status_label_, &lv_font_montserrat_14, 0);

    morse_level_bar_ = lv_bar_create(content_);
    lv_obj_set_width(morse_level_bar_, LV_PCT(100));
    lv_obj_set_height(morse_level_bar_, 8);
    lv_bar_set_range(morse_level_bar_, 0, 100);
    lv_bar_set_value(morse_level_bar_, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(morse_level_bar_, ::ui::theme::surface_alt(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(morse_level_bar_, ::ui::theme::accent(), LV_PART_INDICATOR);

    morse_symbol_label_ = lv_label_create(content_);
    lv_label_set_text(morse_symbol_label_, "Symbol: ");
    lv_obj_set_style_text_color(morse_symbol_label_, ::ui::theme::text(), 0);
    lv_obj_set_style_text_font(morse_symbol_label_, &lv_font_montserrat_16, 0);

    morse_text_label_ = lv_label_create(content_);
    lv_obj_set_width(morse_text_label_, LV_PCT(100));
    lv_label_set_long_mode(morse_text_label_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(morse_text_label_, "Text: ");
    lv_obj_set_style_text_color(morse_text_label_, ::ui::theme::text(), 0);
    lv_obj_set_style_text_font(morse_text_label_, &lv_font_montserrat_16, 0);

    morse_hint_label_ = lv_label_create(content_);
    lv_obj_set_width(morse_hint_label_, LV_PCT(100));
    lv_label_set_long_mode(morse_hint_label_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(morse_hint_label_, "Calibrating...");
    lv_obj_set_style_text_color(morse_hint_label_, ::ui::theme::text_muted(), 0);
    lv_obj_set_style_text_font(morse_hint_label_, &lv_font_montserrat_12, 0);

    morse_back_btn_ = lv_btn_create(content_);
    lv_obj_set_width(morse_back_btn_, LV_PCT(100));
    lv_obj_set_height(morse_back_btn_, kButtonHeight);
    lv_obj_set_style_bg_color(morse_back_btn_, ::ui::theme::surface(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(morse_back_btn_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(morse_back_btn_, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(morse_back_btn_, ::ui::theme::border(), LV_PART_MAIN);
    lv_obj_set_style_radius(morse_back_btn_, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_left(morse_back_btn_, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_right(morse_back_btn_, 12, LV_PART_MAIN);
    lv_obj_clear_flag(morse_back_btn_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(morse_back_btn_, morse_back_event_cb, LV_EVENT_CLICKED, this);
    lv_obj_t* back_label = lv_label_create(morse_back_btn_);
    lv_label_set_text(back_label, "Back");
    lv_obj_align(back_label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_text_color(back_label, ::ui::theme::text(), 0);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_18, 0);

    morse_ = new input::MorseEngine();
    input::MorseEngine::Config cfg;
    cfg.pin_sck = PDM_SCK;
    cfg.pin_data = PDM_DATA;
    cfg.input_gain = 3;
    cfg.min_high = 90;
    cfg.min_low = 45;
    cfg.level_gate = 35;
    cfg.dc_shift = 6;
    cfg.touch_suppress_ms = 180;
    cfg.dash_min_mult = 3;
    cfg.dash_max_mult = 10;
    cfg.release_ms = 30;
    cfg.char_gap_mult = 3;
    cfg.word_gap_mult = 7;
    cfg.release_ms = 25;
    cfg.log_interval_ms = 500;
    cfg.log_calib_only = true;
    if (!morse_ || !morse_->start(cfg))
    {
        delete morse_;
        morse_ = nullptr;
        ::ui::SystemNotification::show("Mic init failed", 1200);
        showMain();
        return;
    }

    morse_timer_ = lv_timer_create(morse_timer_cb, 80, this);
}

void ChatComposeScreen::stopMorse()
{
    if (morse_timer_)
    {
        lv_timer_del(morse_timer_);
        morse_timer_ = nullptr;
    }
    if (morse_)
    {
        morse_->stop();
        delete morse_;
        morse_ = nullptr;
    }
    morse_title_label_ = nullptr;
    morse_status_label_ = nullptr;
    morse_level_bar_ = nullptr;
    morse_symbol_label_ = nullptr;
    morse_text_label_ = nullptr;
    morse_hint_label_ = nullptr;
    morse_back_btn_ = nullptr;
}

void ChatComposeScreen::updateMorseUi()
{
    if (!morse_ || !morse_status_label_ || !morse_level_bar_ || !morse_symbol_label_ || !morse_text_label_ ||
        !morse_hint_label_)
    {
        return;
    }
    input::MorseEngine::Snapshot snap;
    if (!morse_->getSnapshot(snap))
    {
        return;
    }

    std::string status;
    if (snap.calibrated)
    {
        status = "Status: LISTEN";
    }
    else if (snap.phase == input::MorseEngine::CalibPhase::Dash)
    {
        status = std::string("Status: CALIB DASH ") + std::to_string(snap.calib_index) + "/" +
                 std::to_string(snap.calib_total);
    }
    else
    {
        status = std::string("Status: CALIB DOT ") + std::to_string(snap.calib_index) + "/" +
                 std::to_string(snap.calib_total);
    }
    lv_label_set_text(morse_status_label_, status.c_str());

    std::string symbol = "Symbol: ";
    symbol += snap.symbol;
    lv_label_set_text(morse_symbol_label_, symbol.c_str());

    std::string text = "Text: ";
    text += snap.text;
    lv_label_set_text(morse_text_label_, text.c_str());

    if (snap.calibrated)
    {
        lv_label_set_text(morse_hint_label_, "Tap to input, idle 3s to send");
    }
    else
    {
        if (snap.phase == input::MorseEngine::CalibPhase::Dash)
        {
            if (snap.calib_total > 0)
            {
                std::string hint =
                    std::string("Step 2/2: Tap ") + std::to_string(snap.calib_total) + " long dashes";
                lv_label_set_text(morse_hint_label_, hint.c_str());
            }
            else
            {
                lv_label_set_text(morse_hint_label_, "Step 2/2: Tap long dashes");
            }
        }
        else
        {
            if (snap.calib_total > 0)
            {
                std::string hint = std::string("Step 1/2: Tap ") + std::to_string(snap.calib_total) + " short dots";
                lv_label_set_text(morse_hint_label_, hint.c_str());
            }
            else
            {
                lv_label_set_text(morse_hint_label_, "Step 1/2: Tap short dots");
            }
        }
    }
    lv_bar_set_value(morse_level_bar_, snap.level, LV_ANIM_OFF);

    std::string send_text;
    if (morse_->consumeSend(send_text))
    {
        selected_text_ = send_text;
        stopMorse();
        schedule_action_async(ActionIntent::Send);
    }
}

void ChatComposeScreen::schedule_action_async(ActionIntent intent)
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

void ChatComposeScreen::main_event_cb(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }
    auto* screen = static_cast<ChatComposeScreen*>(lv_event_get_user_data(e));
    if (!screen)
    {
        return;
    }
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (target == screen->mic_btn_)
    {
        ::ui::SystemNotification::show("Mic TBD", 1200);
        return;
    }
    if (target == screen->morse_btn_)
    {
        screen->showMorse();
        return;
    }
    if (target == screen->preset_btn_)
    {
        screen->showPreset();
        return;
    }
    if (target == screen->back_btn_)
    {
        handle_back(screen);
    }
}

void ChatComposeScreen::morse_back_event_cb(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }
    auto* screen = static_cast<ChatComposeScreen*>(lv_event_get_user_data(e));
    if (!screen)
    {
        return;
    }
    screen->showMain();
}

void ChatComposeScreen::morse_timer_cb(lv_timer_t* timer)
{
    auto* screen = static_cast<ChatComposeScreen*>(timer->user_data);
    if (!screen)
    {
        return;
    }
    screen->updateMorseUi();
}

void ChatComposeScreen::preset_event_cb(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }
    auto* screen = static_cast<ChatComposeScreen*>(lv_event_get_user_data(e));
    if (!screen)
    {
        return;
    }
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int index = (int)(intptr_t)lv_obj_get_user_data(target);
    if (index < 0)
    {
        screen->showMain();
        return;
    }
    if (index >= static_cast<int>(screen->quick_texts_.size()))
    {
        return;
    }
    screen->selected_text_ = screen->quick_texts_[index];
    screen->schedule_action_async(ActionIntent::Send);
}

void ChatComposeScreen::async_action_cb(void* user_data)
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

void ChatComposeScreen::handle_back(void* user_data)
{
    auto* screen = static_cast<ChatComposeScreen*>(user_data);
    if (!screen)
    {
        return;
    }
    if (screen->view_mode_ == ViewMode::Preset)
    {
        screen->showMain();
        return;
    }
    if (screen->view_mode_ == ViewMode::Morse)
    {
        screen->showMain();
        return;
    }
    if (screen->back_cb_)
    {
        screen->back_cb_(screen->back_cb_user_data_);
        return;
    }
    screen->schedule_action_async(ActionIntent::Cancel);
}

} // namespace chat::ui

#endif

#include "ui/screens/common/placeholder_page.h"

#include "ui/app_runtime.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/ui_theme.h"

#include <string>

namespace
{

void request_exit(const ui::placeholder_page::State* state)
{
    if (state && state->host)
    {
        ui::page::request_exit(state->host);
        return;
    }
    ui_request_exit_to_menu();
}

void back_event_cb(lv_event_t* event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
        return;
    }
    auto* state = static_cast<ui::placeholder_page::State*>(lv_event_get_user_data(event));
    request_exit(state);
}

} // namespace

namespace ui::placeholder_page
{

void show(State& state, lv_obj_t* parent)
{
    if (!parent)
    {
        return;
    }
    if (state.root && lv_obj_is_valid(state.root))
    {
        return;
    }

    const auto& profile = ui::page_profile::current();
    state.root = lv_obj_create(parent);
    lv_obj_set_size(state.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(state.root, ui::theme::white(), 0);
    lv_obj_set_style_bg_opa(state.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(state.root, 0, 0);
    lv_obj_set_style_radius(state.root, 0, 0);
    lv_obj_set_style_pad_left(state.root, 16, 0);
    lv_obj_set_style_pad_right(state.root, 16, 0);
    lv_obj_set_style_pad_top(state.root, 16, 0);
    lv_obj_set_style_pad_bottom(state.root, 16, 0);
    lv_obj_clear_flag(state.root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* back_btn = lv_btn_create(state.root);
    lv_obj_set_size(back_btn,
                    profile.large_touch_hitbox ? 108 : 88,
                    profile.large_touch_hitbox ? 40 : 32);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_event_cb(back_btn, back_event_cb, LV_EVENT_CLICKED, &state);

    lv_obj_t* back_label = lv_label_create(back_btn);
    const std::string back_text = std::string(LV_SYMBOL_LEFT " ") + ::ui::i18n::tr("Back");
    ::ui::i18n::set_label_text_raw(back_label, back_text.c_str());
    lv_obj_center(back_label);

    lv_obj_t* title = lv_label_create(state.root);
    ::ui::i18n::set_label_text(title, state.title ? state.title : "App");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, ui::theme::text(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t* body = lv_label_create(state.root);
    lv_label_set_text(body, state.body ? state.body : "");
    lv_obj_set_width(body, LV_PCT(100));
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(body, ui::theme::text_muted(), 0);
    lv_obj_center(body);
}

void hide(State& state)
{
    if (!state.root || !lv_obj_is_valid(state.root))
    {
        state.root = nullptr;
        state.host = nullptr;
        return;
    }
    lv_obj_del(state.root);
    state.root = nullptr;
    state.host = nullptr;
}

void enter_callback(void* user_data, lv_obj_t* parent)
{
    auto* state = static_cast<State*>(user_data);
    if (!state)
    {
        return;
    }
    show(*state, parent);
}

void exit_callback(void* user_data, lv_obj_t* parent)
{
    (void)parent;
    auto* state = static_cast<State*>(user_data);
    if (!state)
    {
        return;
    }
    hide(*state);
}

} // namespace ui::placeholder_page

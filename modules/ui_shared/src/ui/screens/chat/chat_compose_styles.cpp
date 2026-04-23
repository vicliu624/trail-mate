#if !defined(ARDUINO_T_WATCH_S3)
#include "ui/screens/chat/chat_compose_styles.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/theme/theme_component_style.h"
#include "ui/ui_theme.h"

namespace chat::ui::compose::styles
{

static bool inited = false;

static lv_style_t s_container;
static lv_style_t s_content;
static lv_style_t s_textarea;
static lv_style_t s_action_bar;
static lv_style_t s_btn_basic;
static lv_style_t s_btn_focused;
static lv_style_t s_len_label;

void init_once()
{
    if (inited) return;
    inited = true;

    lv_style_init(&s_container);
    lv_style_set_bg_color(&s_container, ::ui::theme::page_bg());
    lv_style_set_bg_opa(&s_container, LV_OPA_COVER);
    lv_style_set_border_width(&s_container, 0);
    lv_style_set_pad_all(&s_container, 0);
    lv_style_set_radius(&s_container, 0);
    {
        ::ui::theme::ComponentProfile profile{};
        if (::ui::theme::resolve_component_profile(::ui::theme::ComponentSlot::ChatComposeRoot,
                                                   profile))
        {
            ::ui::theme::apply_component_profile_to_style(&s_container, profile);
        }
    }

    lv_style_init(&s_content);
    lv_style_set_bg_color(&s_content, ::ui::theme::page_bg());
    lv_style_set_bg_opa(&s_content, LV_OPA_COVER);
    lv_style_set_radius(&s_content, 0);
    {
        ::ui::theme::ComponentProfile profile{};
        if (::ui::theme::resolve_component_profile(
                ::ui::theme::ComponentSlot::ChatComposeContentRegion,
                profile))
        {
            ::ui::theme::apply_component_profile_to_style(&s_content, profile);
        }
    }

    lv_style_init(&s_textarea);
    lv_style_set_bg_color(&s_textarea, ::ui::theme::surface());
    lv_style_set_bg_opa(&s_textarea, LV_OPA_COVER);
    lv_style_set_text_color(&s_textarea, ::ui::theme::text());
    lv_style_set_text_font(&s_textarea, ::ui::fonts::ui_chrome_font());
    lv_style_set_border_width(&s_textarea, 1);
    lv_style_set_border_color(&s_textarea, ::ui::theme::border());
    lv_style_set_radius(&s_textarea, 6);
    lv_style_set_pad_all(&s_textarea, 10);
    {
        ::ui::theme::ComponentProfile profile{};
        if (::ui::theme::resolve_component_profile(::ui::theme::ComponentSlot::ChatComposeEditor,
                                                   profile))
        {
            ::ui::theme::apply_component_profile_to_style(&s_textarea, profile);
        }
    }

    lv_style_init(&s_action_bar);
    lv_style_set_bg_color(&s_action_bar, ::ui::theme::surface_alt());
    lv_style_set_bg_opa(&s_action_bar, LV_OPA_COVER);
    lv_style_set_border_width(&s_action_bar, 0);
    {
        ::ui::theme::ComponentProfile profile{};
        if (::ui::theme::resolve_component_profile(
                ::ui::theme::ComponentSlot::ChatComposeActionBar,
                profile))
        {
            ::ui::theme::apply_component_profile_to_style(&s_action_bar, profile);
        }
    }

    lv_style_init(&s_btn_basic);
    lv_style_set_bg_color(&s_btn_basic, ::ui::theme::surface());
    lv_style_set_bg_opa(&s_btn_basic, LV_OPA_COVER);
    lv_style_set_border_width(&s_btn_basic, 1);
    lv_style_set_border_color(&s_btn_basic, ::ui::theme::border());
    lv_style_set_radius(&s_btn_basic, 6);
    lv_style_set_text_color(&s_btn_basic, ::ui::theme::text());

    lv_style_init(&s_btn_focused);
    lv_style_set_bg_color(&s_btn_focused, ::ui::theme::accent());
    lv_style_set_outline_width(&s_btn_focused, 0);

    lv_style_init(&s_len_label);
    lv_style_set_text_color(&s_len_label, ::ui::theme::text_muted());
}

void apply_all(const layout::Widgets& w)
{
    init_once();

    lv_obj_add_style(w.container, &s_container, 0);
    lv_obj_add_style(w.content, &s_content, 0);
    lv_obj_add_style(w.textarea, &s_textarea, 0);

    lv_obj_add_style(w.action_bar, &s_action_bar, 0);

    lv_obj_add_style(w.send_btn, &s_btn_basic, LV_PART_MAIN);
    lv_obj_add_style(w.send_btn, &s_btn_focused, LV_STATE_FOCUSED);
    {
        ::ui::theme::ComponentProfile profile{};
        if (::ui::theme::resolve_component_profile(::ui::theme::ComponentSlot::ActionButtonPrimary,
                                                   profile))
        {
            ::ui::theme::apply_component_profile_to_obj(w.send_btn, profile, LV_PART_MAIN);
        }
    }

    if (w.position_btn)
    {
        lv_obj_add_style(w.position_btn, &s_btn_basic, LV_PART_MAIN);
        lv_obj_add_style(w.position_btn, &s_btn_focused, LV_STATE_FOCUSED);
        ::ui::theme::ComponentProfile profile{};
        if (::ui::theme::resolve_component_profile(
                ::ui::theme::ComponentSlot::ActionButtonSecondary,
                profile))
        {
            ::ui::theme::apply_component_profile_to_obj(w.position_btn, profile, LV_PART_MAIN);
        }
    }

    lv_obj_add_style(w.cancel_btn, &s_btn_basic, LV_PART_MAIN);
    lv_obj_add_style(w.cancel_btn, &s_btn_focused, LV_STATE_FOCUSED);
    {
        ::ui::theme::ComponentProfile profile{};
        if (::ui::theme::resolve_component_profile(
                ::ui::theme::ComponentSlot::ActionButtonSecondary,
                profile))
        {
            ::ui::theme::apply_component_profile_to_obj(w.cancel_btn, profile, LV_PART_MAIN);
        }
    }

    lv_obj_add_style(w.len_label, &s_len_label, 0);
}

} // namespace chat::ui::compose::styles

#endif

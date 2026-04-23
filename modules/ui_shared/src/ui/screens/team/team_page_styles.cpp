/**
 * @file team_page_styles.cpp
 * @brief Team page styles
 */

#include "ui/screens/team/team_page_styles.h"
#include "ui/theme/theme_component_style.h"
#include "ui/ui_theme.h"

namespace team
{
namespace ui
{
namespace style
{
namespace
{
static lv_style_t s_root;
static lv_style_t s_header;
static lv_style_t s_content;
static lv_style_t s_body;
static lv_style_t s_actions;
static lv_style_t s_section_label;
static lv_style_t s_meta_label;
static lv_style_t s_list_item;
static lv_style_t s_btn_basic;
static lv_style_t s_btn_focused;
static bool s_inited = false;
static uint32_t s_theme_revision = 0;

lv_style_selector_t selector_for_state(lv_state_t state)
{
    return static_cast<lv_style_selector_t>(static_cast<unsigned>(LV_PART_MAIN) | static_cast<unsigned>(state));
}

void apply_component_profile(lv_style_t* style, ::ui::theme::ComponentSlot slot)
{
    ::ui::theme::ComponentProfile profile{};
    if (::ui::theme::resolve_component_profile(slot, profile))
    {
        ::ui::theme::apply_component_profile_to_style(style, profile);
    }
}

lv_color_t resolve_focus_accent(::ui::theme::ComponentSlot slot)
{
    ::ui::theme::ComponentProfile profile{};
    if (::ui::theme::resolve_component_profile(slot, profile) &&
        profile.accent_color.present)
    {
        return profile.accent_color.value;
    }
    return ::ui::theme::accent();
}
} // namespace

void init_once()
{
    const uint32_t active_revision = ::ui::theme::active_theme_revision();
    if (s_inited && s_theme_revision == active_revision)
    {
        return;
    }

    if (s_inited)
    {
        lv_style_reset(&s_root);
        lv_style_reset(&s_header);
        lv_style_reset(&s_content);
        lv_style_reset(&s_body);
        lv_style_reset(&s_actions);
        lv_style_reset(&s_section_label);
        lv_style_reset(&s_meta_label);
        lv_style_reset(&s_list_item);
        lv_style_reset(&s_btn_basic);
        lv_style_reset(&s_btn_focused);
    }
    else
    {
        s_inited = true;
    }
    s_theme_revision = active_revision;

    lv_style_init(&s_root);
    lv_style_set_bg_color(&s_root, ::ui::theme::page_bg());
    lv_style_set_bg_opa(&s_root, LV_OPA_COVER);
    lv_style_set_border_width(&s_root, 0);
    lv_style_set_pad_all(&s_root, 0);

    lv_style_init(&s_header);
    lv_style_set_bg_color(&s_header, ::ui::theme::page_bg());
    lv_style_set_bg_opa(&s_header, LV_OPA_COVER);
    lv_style_set_border_width(&s_header, 0);
    lv_style_set_pad_all(&s_header, 0);

    lv_style_init(&s_content);
    lv_style_set_bg_color(&s_content, ::ui::theme::page_bg());
    lv_style_set_bg_opa(&s_content, LV_OPA_COVER);
    lv_style_set_border_width(&s_content, 0);
    lv_style_set_pad_all(&s_content, 0);
    lv_style_set_pad_top(&s_content, 3);

    lv_style_init(&s_body);
    lv_style_set_bg_color(&s_body, ::ui::theme::page_bg());
    lv_style_set_bg_opa(&s_body, LV_OPA_COVER);
    lv_style_set_border_width(&s_body, 0);
    lv_style_set_pad_all(&s_body, 6);
    lv_style_set_pad_row(&s_body, 6);
    lv_style_set_pad_column(&s_body, 0);

    lv_style_init(&s_actions);
    lv_style_set_bg_color(&s_actions, ::ui::theme::page_bg());
    lv_style_set_bg_opa(&s_actions, LV_OPA_COVER);
    lv_style_set_border_width(&s_actions, 0);
    lv_style_set_pad_all(&s_actions, 4);
    lv_style_set_pad_row(&s_actions, 0);
    lv_style_set_pad_column(&s_actions, 6);

    lv_style_init(&s_section_label);
    lv_style_set_text_color(&s_section_label, ::ui::theme::text());
    lv_style_set_text_align(&s_section_label, LV_TEXT_ALIGN_LEFT);

    lv_style_init(&s_meta_label);
    lv_style_set_text_color(&s_meta_label, ::ui::theme::text_muted());
    lv_style_set_text_align(&s_meta_label, LV_TEXT_ALIGN_LEFT);

    lv_style_init(&s_list_item);
    lv_style_set_bg_color(&s_list_item, ::ui::theme::surface());
    lv_style_set_bg_opa(&s_list_item, LV_OPA_COVER);
    lv_style_set_border_width(&s_list_item, 1);
    lv_style_set_border_color(&s_list_item, ::ui::theme::border());
    lv_style_set_radius(&s_list_item, 8);
    lv_style_set_pad_all(&s_list_item, 6);
    apply_component_profile(&s_list_item, ::ui::theme::ComponentSlot::DirectoryBrowserListItem);

    lv_style_init(&s_btn_basic);
    lv_style_set_bg_color(&s_btn_basic, ::ui::theme::surface());
    lv_style_set_bg_opa(&s_btn_basic, LV_OPA_COVER);
    lv_style_set_text_color(&s_btn_basic, ::ui::theme::text());
    lv_style_set_border_width(&s_btn_basic, 1);
    lv_style_set_border_color(&s_btn_basic, ::ui::theme::border());
    lv_style_set_radius(&s_btn_basic, 12);
    apply_component_profile(&s_btn_basic, ::ui::theme::ComponentSlot::ActionButtonSecondary);

    lv_style_init(&s_btn_focused);
    lv_style_set_bg_color(&s_btn_focused,
                          resolve_focus_accent(::ui::theme::ComponentSlot::ActionButtonSecondary));
    lv_style_set_bg_opa(&s_btn_focused, LV_OPA_COVER);
}

void apply_root(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_root, 0);
}

void apply_header(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_header, 0);
}

void apply_content(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_content, 0);
}

void apply_body(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_body, 0);
}

void apply_actions(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_actions, 0);
}

void apply_section_label(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_section_label, 0);
}

void apply_meta_label(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_meta_label, 0);
}

void apply_list_item(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_list_item, LV_PART_MAIN);
    lv_obj_add_style(obj, &s_btn_focused, selector_for_state(LV_STATE_FOCUSED));
}

void apply_button_primary(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_btn_basic, LV_PART_MAIN);
    lv_obj_add_style(obj, &s_btn_focused, selector_for_state(LV_STATE_FOCUSED));
}

void apply_button_secondary(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_btn_basic, LV_PART_MAIN);
    lv_obj_add_style(obj, &s_btn_focused, selector_for_state(LV_STATE_FOCUSED));
}

} // namespace style
} // namespace ui
} // namespace team

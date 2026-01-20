/**
 * @file team_page_styles.cpp
 * @brief Team page styles
 */

#include "team_page_styles.h"

namespace team
{
namespace ui
{
namespace style
{
namespace
{
static constexpr uint32_t kBtnBg     = 0xF4C77A;
static constexpr uint32_t kBtnBgFoc  = 0xEBA341;
static constexpr uint32_t kBtnBorder = 0xEBA341;
static constexpr uint32_t kTextMain  = 0x202020;

static lv_style_t s_root;
static lv_style_t s_header;
static lv_style_t s_content;
static lv_style_t s_state_container;
static lv_style_t s_section;
static lv_style_t s_status;
static lv_style_t s_btn_basic;
static lv_style_t s_btn_focused;
static bool s_inited = false;
} // namespace

void init_once()
{
    if (s_inited)
    {
        return;
    }
    s_inited = true;

    lv_style_init(&s_root);
    lv_style_set_bg_opa(&s_root, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_root, 0);
    lv_style_set_pad_all(&s_root, 0);

    lv_style_init(&s_header);
    lv_style_set_bg_color(&s_header, lv_color_white());
    lv_style_set_bg_opa(&s_header, LV_OPA_COVER);
    lv_style_set_border_width(&s_header, 0);
    lv_style_set_pad_all(&s_header, 0);

    lv_style_init(&s_content);
    lv_style_set_bg_opa(&s_content, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_content, 0);
    lv_style_set_pad_all(&s_content, 0);
    lv_style_set_pad_top(&s_content, 3);

    lv_style_init(&s_state_container);
    lv_style_set_bg_opa(&s_state_container, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_state_container, 0);
    lv_style_set_pad_all(&s_state_container, 0);
    lv_style_set_pad_row(&s_state_container, 3);
    lv_style_set_pad_column(&s_state_container, 0);

    lv_style_init(&s_section);
    lv_style_set_bg_opa(&s_section, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_section, 0);
    lv_style_set_pad_all(&s_section, 0);
    lv_style_set_pad_row(&s_section, 3);
    lv_style_set_pad_column(&s_section, 0);

    lv_style_init(&s_status);
    lv_style_set_text_color(&s_status, lv_color_hex(kTextMain));
    lv_style_set_text_align(&s_status, LV_TEXT_ALIGN_CENTER);

    lv_style_init(&s_btn_basic);
    lv_style_set_bg_color(&s_btn_basic, lv_color_hex(kBtnBg));
    lv_style_set_bg_opa(&s_btn_basic, LV_OPA_COVER);
    lv_style_set_text_color(&s_btn_basic, lv_color_hex(kTextMain));
    lv_style_set_border_width(&s_btn_basic, 1);
    lv_style_set_border_color(&s_btn_basic, lv_color_hex(kBtnBorder));
    lv_style_set_radius(&s_btn_basic, 12);

    lv_style_init(&s_btn_focused);
    lv_style_set_bg_color(&s_btn_focused, lv_color_hex(kBtnBgFoc));
    lv_style_set_bg_opa(&s_btn_focused, LV_OPA_COVER);
}

void apply_root(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &s_root, 0);
}

void apply_header(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &s_header, 0);
}

void apply_content(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &s_content, 0);
}

void apply_state_container(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &s_state_container, 0);
}

void apply_idle_container(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &s_section, 0);
}

void apply_active_container(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &s_section, 0);
}

void apply_status_label(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &s_status, 0);
}

void apply_button_primary(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &s_btn_basic, LV_PART_MAIN);
    lv_obj_add_style(obj, &s_btn_focused, LV_PART_MAIN | LV_STATE_FOCUSED);
}

void apply_button_secondary(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &s_btn_basic, LV_PART_MAIN);
    lv_obj_add_style(obj, &s_btn_focused, LV_PART_MAIN | LV_STATE_FOCUSED);
}

} // namespace style
} // namespace ui
} // namespace team

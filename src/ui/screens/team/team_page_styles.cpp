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
static constexpr uint32_t kTextSub   = 0x4A4A4A;
static constexpr uint32_t kListBg    = 0xFFFFFF;
static constexpr uint32_t kListBorder = 0xD2D2D2;

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
} // namespace

void init_once()
{
    if (s_inited)
    {
        return;
    }
    s_inited = true;

    lv_style_init(&s_root);
    lv_style_set_bg_color(&s_root, lv_color_white());
    lv_style_set_bg_opa(&s_root, LV_OPA_COVER);
    lv_style_set_border_width(&s_root, 0);
    lv_style_set_pad_all(&s_root, 0);

    lv_style_init(&s_header);
    lv_style_set_bg_color(&s_header, lv_color_white());
    lv_style_set_bg_opa(&s_header, LV_OPA_COVER);
    lv_style_set_border_width(&s_header, 0);
    lv_style_set_pad_all(&s_header, 0);

    lv_style_init(&s_content);
    lv_style_set_bg_color(&s_content, lv_color_white());
    lv_style_set_bg_opa(&s_content, LV_OPA_COVER);
    lv_style_set_border_width(&s_content, 0);
    lv_style_set_pad_all(&s_content, 0);
    lv_style_set_pad_top(&s_content, 3);

    lv_style_init(&s_body);
    lv_style_set_bg_color(&s_body, lv_color_white());
    lv_style_set_bg_opa(&s_body, LV_OPA_COVER);
    lv_style_set_border_width(&s_body, 0);
    lv_style_set_pad_all(&s_body, 6);
    lv_style_set_pad_row(&s_body, 6);
    lv_style_set_pad_column(&s_body, 0);

    lv_style_init(&s_actions);
    lv_style_set_bg_color(&s_actions, lv_color_white());
    lv_style_set_bg_opa(&s_actions, LV_OPA_COVER);
    lv_style_set_border_width(&s_actions, 0);
    lv_style_set_pad_all(&s_actions, 4);
    lv_style_set_pad_row(&s_actions, 0);
    lv_style_set_pad_column(&s_actions, 6);

    lv_style_init(&s_section_label);
    lv_style_set_text_color(&s_section_label, lv_color_hex(kTextMain));
    lv_style_set_text_align(&s_section_label, LV_TEXT_ALIGN_LEFT);

    lv_style_init(&s_meta_label);
    lv_style_set_text_color(&s_meta_label, lv_color_hex(kTextSub));
    lv_style_set_text_align(&s_meta_label, LV_TEXT_ALIGN_LEFT);

    lv_style_init(&s_list_item);
    lv_style_set_bg_color(&s_list_item, lv_color_hex(kListBg));
    lv_style_set_bg_opa(&s_list_item, LV_OPA_COVER);
    lv_style_set_border_width(&s_list_item, 1);
    lv_style_set_border_color(&s_list_item, lv_color_hex(kListBorder));
    lv_style_set_radius(&s_list_item, 8);
    lv_style_set_pad_all(&s_list_item, 6);

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

void apply_body(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &s_body, 0);
}

void apply_actions(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &s_actions, 0);
}

void apply_section_label(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &s_section_label, 0);
}

void apply_meta_label(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &s_meta_label, 0);
}

void apply_list_item(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &s_list_item, LV_PART_MAIN);
    lv_obj_add_style(obj, &s_btn_focused, LV_PART_MAIN | LV_STATE_FOCUSED);
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

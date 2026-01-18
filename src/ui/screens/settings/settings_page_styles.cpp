/**
 * @file settings_page_styles.cpp
 * @brief Settings styles implementation
 */

#include "settings_page_styles.h"

namespace settings::ui::style {

static constexpr uint32_t kPrimary = 0xEBA341;
static constexpr uint32_t kPrimaryLight = 0xF1B65A;
static constexpr uint32_t kCardBg = 0xF4C77A;
static constexpr uint32_t kSoftBg = 0xF7DCA8;
static constexpr uint32_t kTextMain = 0x202020;
static constexpr uint32_t kTextMuted = 0x606060;

static bool s_inited = false;
static lv_style_t s_panel_side;
static lv_style_t s_panel_main;
static lv_style_t s_btn_basic;
static lv_style_t s_btn_checked;
static lv_style_t s_item_base;
static lv_style_t s_item_focused;
static lv_style_t s_label_primary;
static lv_style_t s_label_muted;
static lv_style_t s_modal_bg;
static lv_style_t s_modal_panel;
static lv_style_t s_modal_btn_checked;

void init_once()
{
    if (s_inited) return;
    s_inited = true;

    lv_style_init(&s_panel_side);
    lv_style_set_bg_opa(&s_panel_side, LV_OPA_COVER);
    lv_style_set_bg_color(&s_panel_side, lv_color_hex(kSoftBg));
    lv_style_set_border_width(&s_panel_side, 0);
    lv_style_set_pad_all(&s_panel_side, 4);

    lv_style_init(&s_panel_main);
    lv_style_set_bg_opa(&s_panel_main, LV_OPA_COVER);
    lv_style_set_bg_color(&s_panel_main, lv_color_hex(kSoftBg));
    lv_style_set_border_width(&s_panel_main, 0);
    lv_style_set_pad_all(&s_panel_main, 4);

    lv_style_init(&s_btn_basic);
    lv_style_set_bg_opa(&s_btn_basic, LV_OPA_COVER);
    lv_style_set_bg_color(&s_btn_basic, lv_color_hex(kCardBg));
    lv_style_set_border_width(&s_btn_basic, 1);
    lv_style_set_border_color(&s_btn_basic, lv_color_hex(kPrimary));
    lv_style_set_radius(&s_btn_basic, 6);

    lv_style_init(&s_btn_checked);
    lv_style_set_bg_opa(&s_btn_checked, LV_OPA_COVER);
    lv_style_set_bg_color(&s_btn_checked, lv_color_hex(kPrimary));

    lv_style_init(&s_item_base);
    lv_style_set_bg_opa(&s_item_base, LV_OPA_COVER);
    lv_style_set_bg_color(&s_item_base, lv_color_hex(kCardBg));
    lv_style_set_border_width(&s_item_base, 1);
    lv_style_set_border_color(&s_item_base, lv_color_hex(kPrimary));
    lv_style_set_radius(&s_item_base, 6);

    lv_style_init(&s_item_focused);
    lv_style_set_bg_opa(&s_item_focused, LV_OPA_COVER);
    lv_style_set_bg_color(&s_item_focused, lv_color_hex(kPrimaryLight));
    lv_style_set_outline_width(&s_item_focused, 2);
    lv_style_set_outline_color(&s_item_focused, lv_color_hex(kPrimary));

    lv_style_init(&s_label_primary);
    lv_style_set_text_color(&s_label_primary, lv_color_hex(kTextMain));

    lv_style_init(&s_label_muted);
    lv_style_set_text_color(&s_label_muted, lv_color_hex(kTextMuted));

    lv_style_init(&s_modal_bg);
    lv_style_set_bg_opa(&s_modal_bg, LV_OPA_COVER);
    lv_style_set_bg_color(&s_modal_bg, lv_color_hex(kSoftBg));

    lv_style_init(&s_modal_panel);
    lv_style_set_bg_opa(&s_modal_panel, LV_OPA_COVER);
    lv_style_set_bg_color(&s_modal_panel, lv_color_hex(kSoftBg));
    lv_style_set_border_width(&s_modal_panel, 2);
    lv_style_set_border_color(&s_modal_panel, lv_color_hex(kPrimary));
    lv_style_set_radius(&s_modal_panel, 8);

    lv_style_init(&s_modal_btn_checked);
    lv_style_set_bg_opa(&s_modal_btn_checked, LV_OPA_COVER);
    lv_style_set_bg_color(&s_modal_btn_checked, lv_color_hex(kPrimaryLight));
}

void apply_panel_side(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_panel_side, 0);
}

void apply_panel_main(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_panel_main, 0);
}

void apply_btn_basic(lv_obj_t* btn)
{
    init_once();
    lv_obj_add_style(btn, &s_btn_basic, LV_PART_MAIN);
}

void apply_btn_filter(lv_obj_t* btn)
{
    init_once();
    lv_obj_add_style(btn, &s_btn_basic, LV_PART_MAIN);
    lv_obj_add_style(btn, &s_btn_checked, LV_PART_MAIN | LV_STATE_CHECKED);
}

void apply_list_item(lv_obj_t* item)
{
    init_once();
    lv_obj_add_style(item, &s_item_base, LV_PART_MAIN);
    lv_obj_add_style(item, &s_item_focused, LV_PART_MAIN | LV_STATE_FOCUSED);
}

void apply_label_primary(lv_obj_t* label)
{
    init_once();
    lv_obj_add_style(label, &s_label_primary, 0);
}

void apply_label_muted(lv_obj_t* label)
{
    init_once();
    lv_obj_add_style(label, &s_label_muted, 0);
}

void apply_modal_bg(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_modal_bg, 0);
}

void apply_modal_panel(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_modal_panel, 0);
}

void apply_btn_modal(lv_obj_t* btn)
{
    init_once();
    lv_obj_add_style(btn, &s_btn_basic, LV_PART_MAIN);
    lv_obj_add_style(btn, &s_modal_btn_checked, LV_PART_MAIN | LV_STATE_CHECKED);
}

} // namespace settings::ui::style

#include "chat_compose_styles.h"
#include "../../assets/fonts/fonts.h"

namespace chat::ui::compose::styles {

static bool inited = false;

static lv_style_t s_container;
static lv_style_t s_content;
static lv_style_t s_textarea;
static lv_style_t s_action_bar;
static lv_style_t s_btn_basic;
static lv_style_t s_btn_focused;
static lv_style_t s_len_label;

void init_once() {
    if (inited) return;
    inited = true;

    lv_style_init(&s_container);
    lv_style_set_bg_color(&s_container, lv_color_white());
    lv_style_set_bg_opa(&s_container, LV_OPA_COVER);
    lv_style_set_border_width(&s_container, 0);
    lv_style_set_pad_all(&s_container, 0);
    lv_style_set_radius(&s_container, 0);

    lv_style_init(&s_content);
    lv_style_set_bg_color(&s_content, lv_color_hex(0xF5F5F5));
    lv_style_set_bg_opa(&s_content, LV_OPA_COVER);
    lv_style_set_radius(&s_content, 0);

    lv_style_init(&s_textarea);
    lv_style_set_bg_color(&s_textarea, lv_color_white());
    lv_style_set_bg_opa(&s_textarea, LV_OPA_COVER);
    lv_style_set_text_color(&s_textarea, lv_color_hex(0x202020));
    lv_style_set_text_font(&s_textarea, &lv_font_noto_cjk_16_2bpp);
    lv_style_set_border_width(&s_textarea, 1);
    lv_style_set_border_color(&s_textarea, lv_color_hex(0xDDDDDD));
    lv_style_set_radius(&s_textarea, 6);
    lv_style_set_pad_all(&s_textarea, 10);

    lv_style_init(&s_action_bar);
    lv_style_set_bg_color(&s_action_bar, lv_color_hex(0xFFF4E0));
    lv_style_set_bg_opa(&s_action_bar, LV_OPA_COVER);
    lv_style_set_border_width(&s_action_bar, 0);

    lv_style_init(&s_btn_basic);
    lv_style_set_bg_color(&s_btn_basic, lv_color_hex(0xF4C77A));
    lv_style_set_bg_opa(&s_btn_basic, LV_OPA_COVER);
    lv_style_set_border_width(&s_btn_basic, 1);
    lv_style_set_border_color(&s_btn_basic, lv_color_hex(0xEBA341));
    lv_style_set_radius(&s_btn_basic, 6);

    lv_style_init(&s_btn_focused);
    lv_style_set_bg_color(&s_btn_focused, lv_color_hex(0xF1B65A));
    lv_style_set_outline_width(&s_btn_focused, 0);

    lv_style_init(&s_len_label);
    lv_style_set_text_color(&s_len_label, lv_color_hex(0x606060));
}

void apply_all(const layout::Widgets& w) {
    init_once();

    lv_obj_add_style(w.container, &s_container, 0);
    lv_obj_add_style(w.content, &s_content, 0);
    lv_obj_add_style(w.textarea, &s_textarea, 0);

    lv_obj_add_style(w.action_bar, &s_action_bar, 0);

    lv_obj_add_style(w.send_btn, &s_btn_basic, LV_PART_MAIN);
    lv_obj_add_style(w.send_btn, &s_btn_focused, LV_STATE_FOCUSED);

    if (w.position_btn)
    {
        lv_obj_add_style(w.position_btn, &s_btn_basic, LV_PART_MAIN);
        lv_obj_add_style(w.position_btn, &s_btn_focused, LV_STATE_FOCUSED);
    }

    lv_obj_add_style(w.cancel_btn, &s_btn_basic, LV_PART_MAIN);
    lv_obj_add_style(w.cancel_btn, &s_btn_focused, LV_STATE_FOCUSED);

    lv_obj_add_style(w.len_label, &s_len_label, 0);
}

} // namespace chat::ui::compose::styles

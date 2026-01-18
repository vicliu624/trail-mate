#include "chat_message_list_styles.h"
#include "../../assets/fonts/fonts.h"

namespace chat::ui::message_list::styles {

static bool inited = false;

static lv_style_t s_root;
static lv_style_t s_panel;

static lv_style_t s_item_btn;
static lv_style_t s_item_btn_focused;

static lv_style_t s_label_name;
static lv_style_t s_label_preview;
static lv_style_t s_label_time;
static lv_style_t s_label_unread;
static lv_style_t s_label_placeholder;

void init_once()
{
    if (inited) return;
    inited = true;

    // Root container
    lv_style_init(&s_root);
    lv_style_set_bg_color(&s_root, lv_color_white());
    lv_style_set_bg_opa(&s_root, LV_OPA_COVER);
    lv_style_set_border_width(&s_root, 0);
    lv_style_set_pad_all(&s_root, 0);
    lv_style_set_radius(&s_root, 0);

    // Panel background
    lv_style_init(&s_panel);
    lv_style_set_bg_color(&s_panel, lv_color_hex(0xF5F5F5));
    lv_style_set_bg_opa(&s_panel, LV_OPA_COVER);
    lv_style_set_border_width(&s_panel, 0);
    lv_style_set_pad_all(&s_panel, 3);
    lv_style_set_radius(&s_panel, 0);

    // Item button
    lv_style_init(&s_item_btn);
    lv_style_set_bg_color(&s_item_btn, lv_color_hex(0xF4C77A));
    lv_style_set_bg_opa(&s_item_btn, LV_OPA_COVER);
    lv_style_set_border_width(&s_item_btn, 1);
    lv_style_set_border_color(&s_item_btn, lv_color_hex(0xEBA341));
    lv_style_set_radius(&s_item_btn, 6);

    lv_style_init(&s_item_btn_focused);
    lv_style_set_bg_color(&s_item_btn_focused, lv_color_hex(0xF1B65A));
    lv_style_set_outline_width(&s_item_btn_focused, 0);

    // Labels
    lv_style_init(&s_label_name);
    lv_style_set_text_color(&s_label_name, lv_color_hex(0x202020));
    lv_style_set_text_font(&s_label_name, &lv_font_noto_cjk_16_2bpp);

    lv_style_init(&s_label_preview);
    lv_style_set_text_color(&s_label_preview, lv_color_hex(0x606060));
    lv_style_set_text_font(&s_label_preview, &lv_font_noto_cjk_16_2bpp);

    lv_style_init(&s_label_time);
    lv_style_set_text_color(&s_label_time, lv_color_hex(0x808080));
    lv_style_set_text_font(&s_label_time, &lv_font_noto_cjk_16_2bpp);

    lv_style_init(&s_label_unread);
    lv_style_set_text_color(&s_label_unread, lv_color_hex(0xEBA341));
    lv_style_set_text_font(&s_label_unread, &lv_font_noto_cjk_16_2bpp);

    lv_style_init(&s_label_placeholder);
    lv_style_set_text_color(&s_label_placeholder, lv_color_hex(0x707070));
    lv_style_set_text_font(&s_label_placeholder, &lv_font_noto_cjk_16_2bpp);
}

void apply_root_container(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_root, 0);
}

void apply_panel(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_panel, 0);
}

void apply_item_btn(lv_obj_t* btn)
{
    init_once();
    lv_obj_add_style(btn, &s_item_btn, LV_PART_MAIN);
    lv_obj_add_style(btn, &s_item_btn_focused, LV_STATE_FOCUSED);
}

void apply_label_name(lv_obj_t* label)      { init_once(); lv_obj_add_style(label, &s_label_name, 0); }
void apply_label_preview(lv_obj_t* label)   { init_once(); lv_obj_add_style(label, &s_label_preview, 0); }
void apply_label_time(lv_obj_t* label)      { init_once(); lv_obj_add_style(label, &s_label_time, 0); }
void apply_label_unread(lv_obj_t* label)    { init_once(); lv_obj_add_style(label, &s_label_unread, 0); }
void apply_label_placeholder(lv_obj_t* label){ init_once(); lv_obj_add_style(label, &s_label_placeholder, 0); }

} // namespace chat::ui::message_list::styles

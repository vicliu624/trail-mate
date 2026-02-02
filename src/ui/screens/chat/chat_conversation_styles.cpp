#include "chat_conversation_styles.h"
#include "../../assets/fonts/fonts.h"

namespace chat::ui::conversation::styles
{

static bool inited = false;

static lv_style_t s_root;
static lv_style_t s_msg_list;
static lv_style_t s_action_bar;

static lv_style_t s_reply_btn;
static lv_style_t s_reply_btn_focused;
static lv_style_t s_reply_label;

static lv_style_t s_row;

static lv_style_t s_bubble_base;
static lv_style_t s_bubble_self;
static lv_style_t s_bubble_other;
static lv_style_t s_bubble_text;
static lv_style_t s_bubble_time;
static lv_style_t s_bubble_status;

static constexpr lv_coord_t kPadX = 8;
static constexpr lv_coord_t kPadY = 6;
static constexpr lv_coord_t kGapY = 6;
static constexpr lv_coord_t kBubblePadX = 10;
static constexpr lv_coord_t kBubblePadY = 6;
static constexpr lv_coord_t kBubbleRadius = 12;

static const lv_color_t kBubbleOther = lv_color_hex(0xE5F0FF);
static const lv_color_t kBubbleSelf = lv_color_hex(0xFFF4E0);
static const lv_color_t kTextColor = lv_color_hex(0x202020);

void init_once()
{
    if (inited) return;
    inited = true;

    lv_style_init(&s_root);
    lv_style_set_bg_color(&s_root, lv_color_white());
    lv_style_set_bg_opa(&s_root, LV_OPA_COVER);
    lv_style_set_border_width(&s_root, 0);
    lv_style_set_pad_all(&s_root, 0);
    lv_style_set_radius(&s_root, 0);

    lv_style_init(&s_msg_list);
    lv_style_set_bg_color(&s_msg_list, lv_color_hex(0xF5F5F5));
    lv_style_set_bg_opa(&s_msg_list, LV_OPA_COVER);
    lv_style_set_border_width(&s_msg_list, 0);
    lv_style_set_pad_left(&s_msg_list, kPadX);
    lv_style_set_pad_right(&s_msg_list, kPadX);
    lv_style_set_pad_top(&s_msg_list, kPadY);
    lv_style_set_pad_bottom(&s_msg_list, kPadY);
    lv_style_set_pad_row(&s_msg_list, kGapY);
    lv_style_set_radius(&s_msg_list, 0);

    lv_style_init(&s_action_bar);
    lv_style_set_bg_color(&s_action_bar, lv_color_hex(0xFFF4E0)); // pale ginger
    lv_style_set_bg_opa(&s_action_bar, LV_OPA_COVER);
    lv_style_set_border_width(&s_action_bar, 0);
    lv_style_set_pad_left(&s_action_bar, 10);
    lv_style_set_pad_right(&s_action_bar, 10);
    lv_style_set_pad_top(&s_action_bar, 4);
    lv_style_set_pad_bottom(&s_action_bar, 4);

    lv_style_init(&s_reply_btn);
    lv_style_set_bg_color(&s_reply_btn, lv_color_hex(0xF4C77A));
    lv_style_set_bg_opa(&s_reply_btn, LV_OPA_COVER);
    lv_style_set_border_width(&s_reply_btn, 1);
    lv_style_set_border_color(&s_reply_btn, lv_color_hex(0xEBA341));
    lv_style_set_radius(&s_reply_btn, 6);

    lv_style_init(&s_reply_btn_focused);
    lv_style_set_bg_color(&s_reply_btn_focused, lv_color_hex(0xF1B65A));
    lv_style_set_outline_width(&s_reply_btn_focused, 0);

    lv_style_init(&s_reply_label);
    lv_style_set_text_color(&s_reply_label, lv_color_hex(0x202020));
    lv_style_set_text_font(&s_reply_label, &lv_font_noto_cjk_16_2bpp);

    lv_style_init(&s_row);
    lv_style_set_bg_opa(&s_row, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_row, 0);
    lv_style_set_pad_top(&s_row, kGapY / 2);
    lv_style_set_pad_bottom(&s_row, kGapY / 2);
    lv_style_set_pad_left(&s_row, 0);
    lv_style_set_pad_right(&s_row, 0);
    lv_style_set_radius(&s_row, 0);
    lv_style_set_pad_column(&s_row, 6);

    lv_style_init(&s_bubble_base);
    lv_style_set_bg_opa(&s_bubble_base, LV_OPA_COVER);
    lv_style_set_border_width(&s_bubble_base, 0);
    lv_style_set_radius(&s_bubble_base, kBubbleRadius);
    lv_style_set_pad_left(&s_bubble_base, kBubblePadX);
    lv_style_set_pad_right(&s_bubble_base, kBubblePadX);
    lv_style_set_pad_top(&s_bubble_base, kBubblePadY);
    lv_style_set_pad_bottom(&s_bubble_base, kBubblePadY);
    lv_style_set_pad_row(&s_bubble_base, 2);
    lv_style_set_pad_column(&s_bubble_base, 0);
    lv_style_set_bg_grad_dir(&s_bubble_base, LV_GRAD_DIR_NONE);

    lv_style_init(&s_bubble_self);
    lv_style_set_bg_color(&s_bubble_self, kBubbleSelf);

    lv_style_init(&s_bubble_other);
    lv_style_set_bg_color(&s_bubble_other, kBubbleOther);

    lv_style_init(&s_bubble_text);
    lv_style_set_text_color(&s_bubble_text, kTextColor);
    lv_style_set_text_align(&s_bubble_text, LV_TEXT_ALIGN_LEFT);
    lv_style_set_text_font(&s_bubble_text, &lv_font_noto_cjk_16_2bpp);

    lv_style_init(&s_bubble_time);
    lv_style_set_text_color(&s_bubble_time, lv_color_hex(0x707070));
    lv_style_set_text_align(&s_bubble_time, LV_TEXT_ALIGN_LEFT);
    lv_style_set_text_font(&s_bubble_time, &lv_font_montserrat_12);

    lv_style_init(&s_bubble_status);
    lv_style_set_text_color(&s_bubble_status, lv_color_hex(0xB00020));
    lv_style_set_text_align(&s_bubble_status, LV_TEXT_ALIGN_LEFT);
    lv_style_set_text_font(&s_bubble_status, &lv_font_montserrat_12);
}

void apply_root(lv_obj_t* root)
{
    init_once();
    lv_obj_add_style(root, &s_root, 0);
}

void apply_msg_list(lv_obj_t* msg_list)
{
    init_once();
    lv_obj_add_style(msg_list, &s_msg_list, 0);
}

void apply_action_bar(lv_obj_t* action_bar)
{
    init_once();
    lv_obj_add_style(action_bar, &s_action_bar, 0);
}

void apply_reply_btn(lv_obj_t* btn)
{
    init_once();
    lv_obj_add_style(btn, &s_reply_btn, LV_PART_MAIN);
    lv_obj_add_style(btn, &s_reply_btn_focused, LV_STATE_FOCUSED);
}

void apply_reply_label(lv_obj_t* label)
{
    init_once();
    lv_obj_add_style(label, &s_reply_label, 0);
}

void apply_message_row(lv_obj_t* row)
{
    init_once();
    lv_obj_add_style(row, &s_row, 0);
}

void apply_bubble(lv_obj_t* bubble, bool is_self)
{
    init_once();
    lv_obj_add_style(bubble, &s_bubble_base, LV_PART_MAIN);
    lv_obj_add_style(bubble, is_self ? &s_bubble_self : &s_bubble_other, LV_PART_MAIN);
}

void apply_bubble_text(lv_obj_t* label)
{
    init_once();
    lv_obj_add_style(label, &s_bubble_text, 0);
}

void apply_bubble_time(lv_obj_t* label)
{
    init_once();
    lv_obj_add_style(label, &s_bubble_time, 0);
}

void apply_bubble_status(lv_obj_t* label)
{
    init_once();
    lv_obj_add_style(label, &s_bubble_status, 0);
}

} // namespace chat::ui::conversation::styles

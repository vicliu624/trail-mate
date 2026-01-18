/**
 * @file top_bar.cpp
 * @brief Shared top bar widget (bright header reused across pages)
 */

#include "top_bar.h"

namespace ui {
namespace widgets {

static void back_event_cb(lv_event_t* e)
{
    TopBar* bar = static_cast<TopBar*>(lv_event_get_user_data(e));
    if (bar != nullptr && bar->back_cb != nullptr) {
        bar->back_cb(bar->back_user_data);
    }
}

void top_bar_init(TopBar& bar, lv_obj_t* parent, const TopBarConfig& config)
{
    bar.container = lv_obj_create(parent);
    lv_obj_set_size(bar.container, LV_PCT(100), config.height);
    lv_obj_set_style_bg_color(bar.container, lv_color_hex(0xEBA341), 0);
    lv_obj_set_style_bg_opa(bar.container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar.container, 0, 0);
    lv_obj_set_style_pad_left(bar.container, 10, 0);
    lv_obj_set_style_pad_right(bar.container, 10, 0);
    lv_obj_set_style_pad_top(bar.container, 6, 0);
    lv_obj_set_style_pad_bottom(bar.container, 6, 0);
    lv_obj_set_style_radius(bar.container, 0, 0);
    lv_obj_clear_flag(bar.container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(bar.container, LV_SCROLLBAR_MODE_OFF);

    // Layout: row flex to allow centered title despite varying right text width
    lv_obj_set_flex_flow(bar.container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar.container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar.container, 6, 0);

    // Back button: reuse override or create a rounded one
    if (config.back_btn_override != nullptr) {
        bar.back_btn = config.back_btn_override;
    } else if (config.create_back) {
        bar.back_btn = lv_btn_create(bar.container);
        lv_obj_set_size(bar.back_btn, 30, 20);
        lv_obj_set_style_bg_color(bar.back_btn, lv_color_hex(0xF1B65A), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar.back_btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(bar.back_btn, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(bar.back_btn, lv_color_hex(0xB0B0B0), LV_PART_MAIN);
        lv_obj_set_style_radius(bar.back_btn, 12, LV_PART_MAIN);  // oval, align with GPS style
        lv_obj_set_style_bg_color(bar.back_btn, lv_color_hex(0xE0E0E0), LV_STATE_FOCUSED);
        lv_obj_set_style_outline_width(bar.back_btn, 0, LV_STATE_FOCUSED);
        lv_obj_align(bar.back_btn, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_add_event_cb(bar.back_btn, back_event_cb, LV_EVENT_CLICKED, &bar);
        lv_obj_t* back_label = lv_label_create(bar.back_btn);
        lv_label_set_text(back_label, LV_SYMBOL_LEFT);
        lv_obj_center(back_label);
        lv_obj_set_style_text_color(back_label, lv_color_hex(0x202020), 0);
    }
    // If an override is used, leave its styling/callbacks untouched.

    // Title label: reuse override or create centered
    if (config.title_label_override != nullptr) {
        bar.title_label = config.title_label_override;
    } else {
        bar.title_label = lv_label_create(bar.container);
        lv_label_set_text(bar.title_label, "");
        lv_label_set_long_mode(bar.title_label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(bar.title_label, lv_color_hex(0x202020), 0);
    }
    lv_obj_set_flex_grow(bar.title_label, 1);
    lv_obj_set_width(bar.title_label, LV_PCT(100));
    lv_obj_set_style_text_align(bar.title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_max_width(bar.title_label, LV_PCT(100), 0);

    bar.right_label = lv_label_create(bar.container);
    lv_label_set_text(bar.right_label, "");
    lv_obj_set_width(bar.right_label, 90);
    lv_label_set_long_mode(bar.right_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(bar.right_label, lv_color_hex(0x606060), 0);
    lv_obj_set_style_text_align(bar.right_label, LV_TEXT_ALIGN_RIGHT, 0);
}

void top_bar_set_title(TopBar& bar, const char* title)
{
    if (bar.title_label != nullptr && title != nullptr) {
        lv_label_set_text(bar.title_label, title);
    }
}

void top_bar_set_right_text(TopBar& bar, const char* text)
{
    if (bar.right_label != nullptr && text != nullptr) {
        lv_label_set_text(bar.right_label, text);
    }
}

void top_bar_set_back_callback(TopBar& bar, void (*cb)(void*), void* user_data)
{
    bar.back_cb = cb;
    bar.back_user_data = user_data;
}

} // namespace widgets
} // namespace ui

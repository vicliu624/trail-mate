#if !defined(ARDUINO_T_WATCH_S3)
/**
 * @file chat_compose_layout.h
 * @brief Chat compose screen layout (structure only)
 *
 * Wireframe (structure only):
 *
 *  鈹屸攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹? *  鈹?TopBar: [Title]              [RSSI]  鈹? *  鈹溾攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹? *  鈹?Content (grow)                        鈹? *  鈹? 鈹屸攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹?鈹? *  鈹? 鈹?TextArea (multi-line, grow)      鈹?鈹? *  鈹? 鈹斺攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹?鈹? *  鈹溾攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹? *  鈹?ActionBar: [Send] [Cancel]     Len:x 鈹? *  鈹斺攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹? *
 * Tree view:
 * container(root, column)
 * 鈹溾攢 top_bar (widget host on container)
 * 鈹溾攢 content (column, grow=1, not scrollable)
 * 鈹? 鈹斺攢 textarea (grow=1)
 * 鈹斺攢 action_bar (row)
 *    鈹溾攢 send_btn
 *    鈹? 鈹斺攢 send_label
 *    鈹溾攢 cancel_btn
 *    鈹? 鈹斺攢 cancel_label
 *    鈹斺攢 len_label
 */
#include "ui/screens/chat/chat_compose_layout.h"

#include <algorithm>

#include "ui/assets/fonts/font_utils.h"
#include "ui/page/page_profile.h"

namespace chat::ui::compose::layout
{

static lv_obj_t* create_btn_with_label(lv_obj_t* parent, int w, int h, const char* text)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    ::ui::fonts::apply_ui_chrome_font(label);
    lv_obj_set_style_text_color(label, lv_color_hex(0x3A2A1A), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_center(label);
    return btn;
}

void create(lv_obj_t* parent, const Spec& spec, Widgets& w)
{
    const auto& profile = ::ui::page_profile::current();
    const int button_height = std::max(spec.btn_h, static_cast<int>(profile.filter_button_height));
    const int action_bar_height = std::max(spec.action_bar_h, button_height + 16);
    const int send_width = std::max(spec.send_w, profile.large_touch_hitbox ? 108 : spec.send_w);
    const int position_width = std::max(spec.position_w, profile.large_touch_hitbox ? 124 : spec.position_w);
    const int cancel_width = std::max(spec.cancel_w, profile.large_touch_hitbox ? 108 : spec.cancel_w);
    const int content_pad = std::max(spec.content_pad, static_cast<int>(profile.content_pad_left));
    const int content_row_pad = std::max(spec.content_row_pad, profile.large_touch_hitbox ? 8 : spec.content_row_pad);
    const int action_pad_lr = std::max(spec.action_pad_lr, profile.large_touch_hitbox ? 14 : spec.action_pad_lr);
    const int action_pad_tb = std::max(spec.action_pad_tb, profile.large_touch_hitbox ? 6 : spec.action_pad_tb);
    const int button_gap = std::max(spec.btn_gap, profile.large_touch_hitbox ? 12 : spec.btn_gap);

    w.container = lv_obj_create(parent);
    lv_obj_set_size(w.container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(w.container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(w.container, 0, 0);

    ::ui::widgets::TopBarConfig top_bar_cfg{};
    top_bar_cfg.height = profile.top_bar_height;
    ::ui::widgets::top_bar_init(w.top_bar, w.container, top_bar_cfg);

    w.content = lv_obj_create(w.container);
    lv_obj_set_size(w.content, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(w.content, 1);
    lv_obj_set_flex_flow(w.content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(w.content, content_row_pad, 0);
    lv_obj_set_style_pad_all(w.content, content_pad, 0);
    lv_obj_clear_flag(w.content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(w.content, LV_SCROLLBAR_MODE_OFF);

    w.textarea = lv_textarea_create(w.content);
    lv_obj_set_width(w.textarea, LV_PCT(100));
    lv_obj_set_flex_grow(w.textarea, 1);

    w.action_bar = lv_obj_create(w.container);
    lv_obj_set_size(w.action_bar, LV_PCT(100), action_bar_height);
    lv_obj_set_style_pad_left(w.action_bar, action_pad_lr, 0);
    lv_obj_set_style_pad_right(w.action_bar, action_pad_lr, 0);
    lv_obj_set_style_pad_top(w.action_bar, action_pad_tb, 0);
    lv_obj_set_style_pad_bottom(w.action_bar, action_pad_tb, 0);

    lv_obj_set_flex_flow(w.action_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(w.action_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    w.send_btn = create_btn_with_label(w.action_bar, send_width, button_height, "Send");
    lv_obj_set_style_pad_right(w.send_btn, button_gap, 0);

    w.position_btn = create_btn_with_label(w.action_bar, position_width, button_height, "Position");
    lv_obj_set_style_pad_right(w.position_btn, button_gap, 0);

    w.cancel_btn = create_btn_with_label(w.action_bar, cancel_width, button_height, "Cancel");

    lv_obj_t* spacer = lv_obj_create(w.action_bar);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

    w.len_label = lv_label_create(w.action_bar);
    lv_label_set_text(w.len_label, "Remain: 233");
    ::ui::fonts::apply_ui_chrome_font(w.len_label);
    lv_obj_set_style_text_color(w.len_label, lv_color_hex(0x6A5646), 0);
}

} // namespace chat::ui::compose::layout

#endif

/**
 * @file chat_compose_layout.h
 * @brief Chat compose screen layout (structure only)
 *
 * Wireframe (structure only):
 *
 *  ┌──────────────────────────────────────┐
 *  │ TopBar: [Title]              [RSSI]  │
 *  ├──────────────────────────────────────┤
 *  │ Content (grow)                        │
 *  │  ┌──────────────────────────────────┐ │
 *  │  │ TextArea (multi-line, grow)      │ │
 *  │  └──────────────────────────────────┘ │
 *  ├──────────────────────────────────────┤
 *  │ ActionBar: [Send] [Cancel]     Len:x │
 *  └──────────────────────────────────────┘
 *
 * Tree view:
 * container(root, column)
 * ├─ top_bar (widget host on container)
 * ├─ content (column, grow=1, not scrollable)
 * │  └─ textarea (grow=1)
 * └─ action_bar (row)
 *    ├─ send_btn
 *    │  └─ send_label
 *    ├─ cancel_btn
 *    │  └─ cancel_label
 *    └─ len_label
 */
#include "chat_compose_layout.h"

namespace chat::ui::compose::layout
{

static lv_obj_t* create_btn_with_label(lv_obj_t* parent, int w, int h, const char* text)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

void create(lv_obj_t* parent, const Spec& spec, Widgets& w)
{
    w.container = lv_obj_create(parent);
    lv_obj_set_size(w.container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(w.container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(w.container, 0, 0);

    ::ui::widgets::top_bar_init(w.top_bar, w.container);

    w.content = lv_obj_create(w.container);
    lv_obj_set_size(w.content, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(w.content, 1);
    lv_obj_set_flex_flow(w.content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(w.content, spec.content_row_pad, 0);
    lv_obj_set_style_pad_all(w.content, spec.content_pad, 0);
    lv_obj_clear_flag(w.content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(w.content, LV_SCROLLBAR_MODE_OFF);

    w.textarea = lv_textarea_create(w.content);
    lv_obj_set_width(w.textarea, LV_PCT(100));
    lv_obj_set_flex_grow(w.textarea, 1);

    w.action_bar = lv_obj_create(w.container);
    lv_obj_set_size(w.action_bar, LV_PCT(100), spec.action_bar_h);
    lv_obj_set_style_pad_left(w.action_bar, spec.action_pad_lr, 0);
    lv_obj_set_style_pad_right(w.action_bar, spec.action_pad_lr, 0);
    lv_obj_set_style_pad_top(w.action_bar, spec.action_pad_tb, 0);
    lv_obj_set_style_pad_bottom(w.action_bar, spec.action_pad_tb, 0);

    lv_obj_set_flex_flow(w.action_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(w.action_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    w.send_btn = create_btn_with_label(w.action_bar, spec.send_w, spec.btn_h, "Send");
    lv_obj_set_style_pad_right(w.send_btn, spec.btn_gap, 0);

    w.position_btn = create_btn_with_label(w.action_bar, spec.position_w, spec.btn_h, "Position");
    lv_obj_set_style_pad_right(w.position_btn, spec.btn_gap, 0);

    w.cancel_btn = create_btn_with_label(w.action_bar, spec.cancel_w, spec.btn_h, "Cancel");

    lv_obj_t* spacer = lv_obj_create(w.action_bar);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

    w.len_label = lv_label_create(w.action_bar);
    lv_label_set_text(w.len_label, "Remain: 233");
}

} // namespace chat::ui::compose::layout

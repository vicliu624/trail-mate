#pragma once

#include "lvgl.h"

namespace settings::ui::item_layout
{
// Use the row's actual content width, not a board identity or screen width.
constexpr lv_coord_t kInlineMinWidth = 280;

inline void update(lv_event_t* event)
{
    lv_obj_t* row = lv_event_get_target_obj(event);
    if (lv_obj_get_child_count(row) != 2) return;
    lv_obj_t* name = lv_obj_get_child(row, 0);
    lv_obj_t* value = lv_obj_get_child(row, 1);
    const bool has_value = !lv_obj_has_flag(value, LV_OBJ_FLAG_HIDDEN);
    const bool stacked = has_value && (lv_obj_get_content_width(row) < kInlineMinWidth ||
                                       lv_label_get_long_mode(value) == LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_flow(row, stacked ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          stacked ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(row, stacked ? 4 : 0, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_width(name, stacked || !has_value ? LV_PCT(100) : 0);
    lv_obj_set_flex_grow(name, stacked || !has_value ? 0 : 1);
    lv_obj_set_width(value, stacked ? LV_PCT(100) : LV_PCT(48));
    lv_obj_set_style_text_align(value, stacked ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT, 0);
    // A title strip establishes hierarchy without adding a font or focus target.
    lv_obj_set_style_bg_opa(name, stacked ? LV_OPA_20 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(name, lv_obj_get_style_border_color(row, LV_PART_MAIN), 0);
    lv_obj_set_style_pad_left(name, stacked ? 3 : 0, 0);
    lv_obj_set_style_pad_top(name, stacked ? 2 : 0, 0);
    lv_obj_set_style_pad_bottom(name, stacked ? 2 : 0, 0);
}

inline void apply(lv_obj_t* row, lv_obj_t* name, lv_obj_t* value,
                  lv_coord_t minimum_height, bool has_value, bool multiline_value = false)
{
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, minimum_height, 0);
    lv_obj_set_style_pad_top(row, 5, 0);
    lv_obj_set_style_pad_bottom(row, 5, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(value, multiline_value ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
    if (!has_value) lv_obj_add_flag(value, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(row, update, LV_EVENT_SIZE_CHANGED, nullptr);
    lv_obj_send_event(row, LV_EVENT_SIZE_CHANGED, nullptr);
}
} // namespace settings::ui::item_layout

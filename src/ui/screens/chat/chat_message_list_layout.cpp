/**
 * @file chat_message_list_layout.cpp
 * @brief Layout (structure only) for ChatMessageListScreen
 *
 * UI Wireframe / Layout Tree (ChatMessageListScreen)
 * --------------------------------------------------------------------
 *
 * Root Container (COLUMN, full screen)
 *
 * ┌───────────────────────────────────────────────────────────────────┐
 * │  TopBar (fixed height, external widget)                            │
 * │  ┌─────────────────────────────────────────────────────────────┐  │
 * │  │  < Back     MESSAGES                          --:--  --%     │  │
 * │  └─────────────────────────────────────────────────────────────┘  │
 * │                                                                   │
 * │  Panel (COLUMN, flex-grow = 1, light gray background)              │
 * │  ┌─────────────────────────────────────────────────────────────┐  │
 * │  │  Message Item (Button, height=36)                            │  │
 * │  │  ┌────────────────────────────────────────────────────────┐ │  │
 * │  │  │ Name (x=10)   Preview (x=120, w=130, DOT)        12:34 │ │  │
 * │  │  │                                        Unread (x=-42)  │ │  │
 * │  │  └────────────────────────────────────────────────────────┘ │  │
 * │  │                                                             │  │
 * │  │  Message Item × N                                            │  │
 * │  │  ...                                                        │  │
 * │  │                                                             │  │
 * │  │  (Empty State)                                               │  │
 * │  │     "No messages" (centered)                                 │  │
 * │  └─────────────────────────────────────────────────────────────┘  │
 * └───────────────────────────────────────────────────────────────────┘
 *
 * Tree view:
 * ------------------------------------------------------------
 * Root (COLUMN, full screen)
 * ├─ TopBar (external widget)
 * └─ Panel (COLUMN, grow=1)
 *    ├─ MessageItemBtn (repeated)
 *    │   ├─ NameLabel   (left)
 *    │   ├─ PreviewLabel(left, clipped)
 *    │   ├─ UnreadLabel (right, near time)
 *    │   └─ TimeLabel   (right)
 *    └─ (or) PlaceholderLabel ("No messages", centered)
 *
 * Key layout constraints:
 * - Panel: pad_all=3, pad_left/right=5, pad_row=2, non-scrollable
 * - ItemBtn: height=36, full width
 * - NameLabel:   ALIGN_LEFT_MID  x=+10
 * - PreviewLabel:ALIGN_LEFT_MID  x=+120, width=130, LONG_DOT
 * - TimeLabel:   ALIGN_RIGHT_MID x=-10
 * - UnreadLabel: ALIGN_RIGHT_MID x=-42


 * Notes:
 * - Structure only: create objects, set flex/size/align/flags.
 * - Styles are applied via chat_message_list_styles.*.
 */

#include <Arduino.h>
#include "chat_message_list_layout.h"

namespace chat::ui::layout {

static void make_non_scrollable(lv_obj_t* obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

lv_obj_t* create_root(lv_obj_t* parent)
{
    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0); // layout padding only
    make_non_scrollable(root);
    return root;
}

lv_obj_t* create_panel(lv_obj_t* parent)
{
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_size(panel, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // layout spacing only
    lv_obj_set_style_pad_row(panel, 2, 0);
    lv_obj_set_style_pad_left(panel, 5, 0);
    lv_obj_set_style_pad_right(panel, 5, 0);
    lv_obj_set_style_pad_all(panel, 3, 0);

    make_non_scrollable(panel);
    return panel;
}

MessageItemWidgets create_message_item(lv_obj_t* parent)
{
    MessageItemWidgets w{};
    w.btn = lv_btn_create(parent);
    lv_obj_set_size(w.btn, LV_PCT(100), 36);
    make_non_scrollable(w.btn);

    w.name_label = lv_label_create(w.btn);
    lv_obj_align(w.name_label, LV_ALIGN_LEFT_MID, 10, 0);

    w.preview_label = lv_label_create(w.btn);
    lv_obj_align(w.preview_label, LV_ALIGN_LEFT_MID, 120, 0);
    lv_label_set_long_mode(w.preview_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(w.preview_label, 130);

    w.time_label = lv_label_create(w.btn);
    lv_obj_align(w.time_label, LV_ALIGN_RIGHT_MID, -10, 0);

    w.unread_label = lv_label_create(w.btn);
    lv_obj_align(w.unread_label, LV_ALIGN_RIGHT_MID, -42, 0);

    return w;
}

lv_obj_t* create_placeholder(lv_obj_t* parent)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    return label;
}

} // namespace chat::ui::layout

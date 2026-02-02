/**
 * @file chat_conversation_layout.cpp
 * @brief Layout (structure only) for ChatConversationScreen
 *
 * UI Wireframe / Layout Tree
 * --------------------------------------------------------------------
 *
 * Root Container (COLUMN, full screen)
 *
 * ┌───────────────────────────────────────────────────────────────────┐
 * │  TopBar widget (fixed height)                                      │
 * │  ┌─────────────────────────────────────────────────────────────┐  │
 * │  │ < Back     (Title)                            (Status/...)  │  │
 * │  └─────────────────────────────────────────────────────────────┘  │
 * │                                                                   │
 * │  Msg List (scrollable V, flex-grow = 1)                            │
 * │  ┌─────────────────────────────────────────────────────────────┐  │
 * │  │ Row (full width, transparent)                                │  │
 * │  │   └─ Bubble (max ~70% width)                                  │  │
 * │  │       └─ TextLabel (WRAP)                                     │  │
 * │  │ (self -> row align END / other -> row align START)            │  │
 * │  └─────────────────────────────────────────────────────────────┘  │
 * │                                                                   │
 * │  Action Bar (fixed height=30, non-scrollable)                      │
 * │  ┌─────────────────────────────────────────────────────────────┐  │
 * │  │              [ Reply ]                                       │  │
 * │  └─────────────────────────────────────────────────────────────┘  │
 * └───────────────────────────────────────────────────────────────────┘
 *
 * Tree view:
 * Root(COL)
 * ├─ TopBar(widget)    // created by top_bar_init(top_bar_, root)
 * ├─ MsgList(COL, scroll V, grow=1)
 * │   └─ MsgRow*(repeat, ROW, full)
 * │       └─ Bubble(COL, content) -> TextLabel(WRAP)
 * └─ ActionBar(ROW, fixed=30) -> ReplyBtn -> ReplyLabel
 *
 * Notes:
 * - Structure/layout only: create objects, set size/flex/align/flags.
 * - Visual style (colors/radius/padding) lives in styles.*.
 */

#include "chat_conversation_layout.h"
#include <Arduino.h>

namespace chat::ui::layout
{

static void make_non_scrollable(lv_obj_t* obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

ConversationWidgets create_conversation_base(lv_obj_t* parent)
{
    ConversationWidgets w{};

    // Root container (full screen, column)
    w.root = lv_obj_create(parent);
    lv_obj_set_size(w.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(w.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(w.root, 0, 0);
    make_non_scrollable(w.root);

    // Msg list (scrollable, grow=1)
    w.msg_list = lv_obj_create(w.root);
    lv_obj_set_width(w.msg_list, LV_PCT(100));
    lv_obj_set_flex_grow(w.msg_list, 1);
    lv_obj_set_flex_flow(w.msg_list, LV_FLEX_FLOW_COLUMN);

    // Allow vertical scroll only
    lv_obj_set_scroll_dir(w.msg_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(w.msg_list, LV_SCROLLBAR_MODE_OFF);

    // Action bar (fixed height)
    w.action_bar = lv_obj_create(w.root);
    lv_obj_set_size(w.action_bar, LV_PCT(100), 30);
    lv_obj_set_flex_grow(w.action_bar, 0);
    lv_obj_set_flex_flow(w.action_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(w.action_bar,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    make_non_scrollable(w.action_bar);

    // Reply button
    w.reply_btn = lv_btn_create(w.action_bar);
    lv_obj_set_size(w.reply_btn, 120, 24);
    make_non_scrollable(w.reply_btn);

    w.reply_label = lv_label_create(w.reply_btn);
    lv_obj_center(w.reply_label);

    return w;
}

lv_obj_t* create_message_row(lv_obj_t* msg_list_parent)
{
    lv_obj_t* row = lv_obj_create(msg_list_parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    make_non_scrollable(row);
    return row;
}

lv_obj_t* create_bubble(lv_obj_t* row_parent)
{
    lv_obj_t* bubble = lv_obj_create(row_parent);
    lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(bubble, LV_SIZE_CONTENT);
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(bubble, 0);
    return bubble;
}

lv_obj_t* create_bubble_text(lv_obj_t* bubble_parent)
{
    lv_obj_t* label = lv_label_create(bubble_parent);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    return label;
}

lv_obj_t* create_bubble_time(lv_obj_t* bubble_parent)
{
    lv_obj_t* label = lv_label_create(bubble_parent);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    return label;
}

lv_obj_t* create_bubble_status(lv_obj_t* bubble_parent)
{
    lv_obj_t* label = lv_label_create(bubble_parent);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    return label;
}

void align_message_row(lv_obj_t* row, bool is_self)
{
    // Match original behavior:
    // self -> row aligns to END, other -> START
    if (is_self)
    {
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
    }
    else
    {
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
    }
}

void set_bubble_max_width(lv_obj_t* bubble, lv_coord_t max_w)
{
    lv_obj_set_style_max_width(bubble, max_w, LV_PART_MAIN);
}

lv_coord_t get_msg_list_content_width(lv_obj_t* msg_list)
{
    return lv_obj_get_content_width(msg_list);
}

} // namespace chat::ui::layout

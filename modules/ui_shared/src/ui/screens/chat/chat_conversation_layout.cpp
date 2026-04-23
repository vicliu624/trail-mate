#if !defined(ARDUINO_T_WATCH_S3)
/**
 * @file chat_conversation_layout.cpp
 * @brief Layout (structure only) for ChatConversationScreen
 *
 * UI Wireframe / Layout Tree
 * --------------------------------------------------------------------
 *
 * Root Container (COLUMN, full screen)
 *
 *  +----------------------------------------------------------------+
 *  | TopBar widget (fixed height)                                   |
 *  | +------------------------------------------------------------+ |
 *  | | < Back     (Title)                     (Status/...)       | |
 *  | +------------------------------------------------------------+ |
 *  |                                                                |
 *  | Msg List (scrollable V, flex-grow = 1)                         |
 *  | +------------------------------------------------------------+ |
 *  | | Row (full width, transparent)                              | |
 *  | |   + Bubble (max ~70% width)                                | |
 *  | |     + TextLabel (WRAP)                                     | |
 *  | | self -> row align END / other -> row align START           | |
 *  | +------------------------------------------------------------+ |
 *  |                                                                |
 *  | Action Bar (fixed height = 30, non-scrollable)                |
 *  | +------------------------------------------------------------+ |
 *  | |                     [ Reply ]                              | |
 *  | +------------------------------------------------------------+ |
 *  +----------------------------------------------------------------+
 *
 * Tree view:
 * Root(COL)
 * - TopBar(widget)    // created by top_bar_init(top_bar_, root)
 * - MsgList(COL, scroll V, grow=1)
 *   - MsgRow*(repeat, ROW, full)
 *     - Bubble(COL, content) -> TextLabel(WRAP)
 * - ActionBar(ROW, fixed=30) -> ReplyBtn -> ReplyLabel
 *
 * Notes:
 * - Structure/layout only: create objects, set size/flex/align/flags.
 * - Visual style (colors/radius/padding) lives in styles.*.
 */

#include "ui/screens/chat/chat_conversation_layout.h"
#include "ui/presentation/chat_thread_layout.h"

namespace chat::ui::layout
{
namespace thread_layout = ::ui::presentation::chat_thread_layout;

ConversationWidgets create_conversation_base(lv_obj_t* parent)
{
    ConversationWidgets w{};

    w.root = thread_layout::create_root(parent);
    w.msg_list = thread_layout::create_thread_region(w.root);
    w.action_bar = thread_layout::create_action_bar(w.root);
    w.reply_btn = thread_layout::create_action_button(w.action_bar, w.reply_label);

    return w;
}

lv_obj_t* create_message_row(lv_obj_t* msg_list_parent)
{
    return thread_layout::create_thread_row(msg_list_parent);
}

lv_obj_t* create_bubble(lv_obj_t* row_parent)
{
    return thread_layout::create_bubble(row_parent);
}

lv_obj_t* create_bubble_text(lv_obj_t* bubble_parent)
{
    return thread_layout::create_bubble_text(bubble_parent);
}

lv_obj_t* create_bubble_time(lv_obj_t* bubble_parent)
{
    return thread_layout::create_bubble_time(bubble_parent);
}

lv_obj_t* create_bubble_status(lv_obj_t* bubble_parent)
{
    return thread_layout::create_bubble_status(bubble_parent);
}

void align_message_row(lv_obj_t* row, bool is_self)
{
    thread_layout::align_thread_row(row, is_self);
}

void set_bubble_max_width(lv_obj_t* bubble, lv_coord_t max_w)
{
    thread_layout::set_bubble_max_width(bubble, max_w);
}

lv_coord_t get_msg_list_content_width(lv_obj_t* msg_list)
{
    return thread_layout::thread_content_width(msg_list);
}

} // namespace chat::ui::layout

#endif

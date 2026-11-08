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
 *  | BodyRow (ROW, flex-grow = 1)                                   |
 *  | +------------------------------------------------------------+ |
 *  | | Optional location map panel + RightColumn                   | |
 *  | | RightColumn: MsgList(grow=1) + ActionBar(fixed height)      | |
 *  | +------------------------------------------------------------+ |
 *  +----------------------------------------------------------------+
 *
 * Tree view:
 * Root(COL)
 * - TopBar(widget)    // created by top_bar_init(top_bar_, root)
 * - BodyRow(ROW, grow=1)
 *   - LocationPanel(optional)
 *   - RightColumn(COL, grow=1)
 *     - MsgList(COL, scroll V, grow=1)
 *       - MsgRow*(repeat, ROW, full)
 *         - Bubble(COL, content) -> TextLabel(WRAP)
 *     - ActionBar(ROW, fixed=30) -> ComposeBtn -> ComposeLabel
 *
 * Notes:
 * - Structure/layout only: create objects, set size/flex/align/flags.
 * - Visual style (colors/radius/padding) lives in styles.*.
 */

#include "ui/screens/chat/chat_conversation_layout.h"
#include "ui/page/page_profile.h"

namespace chat::ui::layout
{

static void make_non_scrollable(lv_obj_t* obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static void make_plain_container(lv_obj_t* obj)
{
    make_non_scrollable(obj);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
}

static lv_obj_t* create_action_bar_spacer(lv_obj_t* parent)
{
    lv_obj_t* spacer = lv_obj_create(parent);
    lv_obj_set_size(spacer, 0, LV_PCT(100));
    lv_obj_set_flex_grow(spacer, 1);
    make_plain_container(spacer);
    return spacer;
}

static void style_help_chip(lv_obj_t* chip,
                            const ::ui::page_profile::PageLayoutProfile& profile)
{
    lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(chip, lv_color_hex(0xFFE36E), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_border_color(chip, lv_color_hex(0xC28700), 0);
    lv_obj_set_style_radius(chip, 10, 0);
    lv_obj_set_style_shadow_width(chip, 0, 0);
    lv_obj_set_style_pad_left(chip, profile.dense ? 6 : 8, 0);
    lv_obj_set_style_pad_right(chip, profile.dense ? 6 : 8, 0);
    lv_obj_set_style_pad_top(chip, profile.dense ? 2 : 3, 0);
    lv_obj_set_style_pad_bottom(chip, profile.dense ? 2 : 3, 0);
    lv_obj_set_style_min_height(
        chip,
#if defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
        18,
#else
        ::ui::page_profile::resolve_control_button_height() - (profile.dense ? 4 : 6),
#endif
        0);
#if defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
    lv_obj_set_style_pad_top(chip, 1, 0);
    lv_obj_set_style_pad_bottom(chip, 1, 0);
    lv_obj_set_style_radius(chip, 3, 0);
#endif
    make_non_scrollable(chip);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_CLICKABLE);
}

ConversationWidgets create_conversation_base(lv_obj_t* parent)
{
    ConversationWidgets w{};
    const auto& profile = ::ui::page_profile::current();

    // Root container (full screen, column)
    w.root = lv_obj_create(parent);
    lv_obj_set_size(w.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(w.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(w.root, 0, 0);
    make_non_scrollable(w.root);

    // Body row holds the optional location panel plus the message/actions column.
    w.body_row = lv_obj_create(w.root);
    lv_obj_set_width(w.body_row, LV_PCT(100));
    lv_obj_set_flex_grow(w.body_row, 1);
    lv_obj_set_flex_flow(w.body_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(w.body_row, 0, 0);
    lv_obj_set_style_pad_row(w.body_row, 0, 0);
    make_plain_container(w.body_row);

    w.right_column = lv_obj_create(w.body_row);
    lv_obj_set_width(w.right_column, 1);
    lv_obj_set_height(w.right_column, LV_PCT(100));
    lv_obj_set_flex_grow(w.right_column, 1);
    lv_obj_set_flex_flow(w.right_column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(w.right_column, 0, 0);
    make_plain_container(w.right_column);

    // Msg list (scrollable, grow=1)
    w.msg_list = lv_obj_create(w.right_column);
    lv_obj_set_width(w.msg_list, LV_PCT(100));
    lv_obj_set_flex_grow(w.msg_list, 1);
    lv_obj_set_flex_flow(w.msg_list, LV_FLEX_FLOW_COLUMN);

    // Allow vertical scroll only
    lv_obj_set_scroll_dir(w.msg_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(w.msg_list, LV_SCROLLBAR_MODE_OFF);

    // Action bar (fixed height)
    w.action_bar = lv_obj_create(w.right_column);
    lv_obj_set_size(w.action_bar, LV_PCT(100),
#if defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
                    24);
#else
                    ::ui::page_profile::resolve_control_button_height() +
                        (profile.dense ? 2 : 2));
#endif
    lv_obj_set_flex_grow(w.action_bar, 0);
    lv_obj_set_flex_flow(w.action_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(w.action_bar,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    make_non_scrollable(w.action_bar);

    w.help_chip = lv_obj_create(w.action_bar);
    style_help_chip(w.help_chip, profile);
    w.help_label = lv_label_create(w.help_chip);
    lv_obj_set_width(w.help_label, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(w.help_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(w.help_label, lv_color_hex(0x3A2A1A), 0);
    lv_obj_set_style_text_font(
        w.help_label,
        profile.caption_font ? profile.caption_font : ::ui::page_profile::resolve_caption_font(),
        0);
    lv_label_set_long_mode(w.help_label, LV_LABEL_LONG_CLIP);
    lv_label_set_recolor(w.help_label, true);
#if defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
    lv_label_set_text(w.help_label, "#D00000 S# Send  #D00000 H# Help");
#else
    lv_label_set_text(w.help_label, "#D00000 H#elp");
#endif
    lv_obj_center(w.help_label);

    create_action_bar_spacer(w.action_bar);

    // Primary compose button
    w.reply_btn = lv_btn_create(w.action_bar);
#if defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
    lv_obj_set_size(w.reply_btn, 54, 20);
    lv_obj_set_style_pad_all(w.reply_btn, 2, 0);
    lv_obj_set_style_text_font(w.reply_btn, ::ui::page_profile::resolve_caption_font(), 0);
#else
    lv_obj_set_size(w.reply_btn,
                    profile.dense ? 74 : 96,
                    ::ui::page_profile::resolve_control_button_height());
#endif
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

#endif

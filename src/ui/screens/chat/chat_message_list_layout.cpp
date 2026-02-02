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

#include "chat_message_list_layout.h"
#include <Arduino.h>

namespace chat::ui::layout
{

static void make_non_scrollable(lv_obj_t* obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

namespace
{
constexpr int kFilterPanelWidth = 80;
constexpr int kButtonHeight = 32;
constexpr int kPanelGap = 0;
constexpr int kScreenEdgePadding = 0;
} // namespace

lv_obj_t* create_root(lv_obj_t* parent)
{
    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 3, 0);
    lv_obj_set_style_pad_all(root, 0, 0); // layout padding only
    make_non_scrollable(root);
    return root;
}

static lv_obj_t* create_content(lv_obj_t* parent)
{
    lv_obj_t* content = lv_obj_create(parent);
    lv_obj_set_size(content, LV_PCT(100), 0);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_left(content, kScreenEdgePadding, 0);
    lv_obj_set_style_pad_right(content, kScreenEdgePadding, 0);
    lv_obj_set_style_pad_top(content, 0, 0);
    lv_obj_set_style_pad_bottom(content, 0, 0);
    make_non_scrollable(content);
    return content;
}

static lv_obj_t* create_filter_panel(lv_obj_t* parent,
                                     lv_obj_t** direct_btn,
                                     lv_obj_t** broadcast_btn,
                                     lv_obj_t** team_btn)
{
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_width(panel, kFilterPanelWidth);
    lv_obj_set_height(panel, LV_PCT(100));
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, 3, LV_PART_MAIN);
    lv_obj_set_style_margin_right(panel, kPanelGap, LV_PART_MAIN);
    make_non_scrollable(panel);

    lv_obj_t* direct = lv_btn_create(panel);
    lv_obj_set_size(direct, LV_PCT(100), kButtonHeight);
    make_non_scrollable(direct);
    lv_obj_t* direct_label = lv_label_create(direct);
    lv_label_set_text(direct_label, "Direct");
    lv_obj_center(direct_label);

    lv_obj_t* broadcast = lv_btn_create(panel);
    lv_obj_set_size(broadcast, LV_PCT(100), kButtonHeight);
    make_non_scrollable(broadcast);
    lv_obj_t* broadcast_label = lv_label_create(broadcast);
    lv_label_set_text(broadcast_label, "Broadcast");
    lv_obj_center(broadcast_label);

    lv_obj_t* team = lv_btn_create(panel);
    lv_obj_set_size(team, LV_PCT(100), kButtonHeight);
    make_non_scrollable(team);
    lv_obj_t* team_label = lv_label_create(team);
    lv_label_set_text(team_label, "Team");
    lv_obj_center(team_label);
    lv_obj_add_flag(team, LV_OBJ_FLAG_HIDDEN);

    if (direct_btn) *direct_btn = direct;
    if (broadcast_btn) *broadcast_btn = broadcast;
    if (team_btn) *team_btn = team;
    return panel;
}

static lv_obj_t* create_list_panel(lv_obj_t* parent)
{
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_height(panel, LV_PCT(100));
    lv_obj_set_width(panel, 0);
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // layout spacing only
    lv_obj_set_style_pad_row(panel, 3, 0);
    lv_obj_set_style_pad_left(panel, 5, 0);
    lv_obj_set_style_pad_right(panel, 5, 0);
    lv_obj_set_style_pad_all(panel, 3, 0);

    make_non_scrollable(panel);
    return panel;
}

MessageListLayout create_layout(lv_obj_t* parent)
{
    MessageListLayout w{};
    w.root = create_root(parent);
    w.content = create_content(w.root);
    w.filter_panel = create_filter_panel(w.content, &w.direct_btn, &w.broadcast_btn, &w.team_btn);
    w.list_panel = create_list_panel(w.content);
    return w;
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

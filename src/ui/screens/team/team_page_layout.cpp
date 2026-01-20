/**
 * UI Wireframe / Layout Tree
 * --------------------------------------------------------------------
 *
 * Root (COLUMN)
 * ├─ Header (TopBar)
 * └─ Content (flex-grow = 1)
 *    └─ StateContainer (COLUMN, centered)
 *       ├─ IdleContainer
 *       │  ├─ IdleStatusLabel
 *       │  ├─ CreateBtn
 *       │  └─ JoinBtn
 *       └─ ActiveContainer
 *          ├─ ActiveStatusLabel
 *          ├─ ViewMapBtn
 *          ├─ ShareBtn
 *          ├─ LeaveBtn
 *          └─ DisbandBtn
 *
 * Tree view:
 * Root(COL)
 * ├─ Header
 * └─ Content(COL)
 *    └─ StateContainer(COL)
 *       ├─ IdleContainer(COL) -> IdleStatusLabel, CreateBtn, JoinBtn
 *       └─ ActiveContainer(COL) -> ActiveStatusLabel, ViewMapBtn, ShareBtn, LeaveBtn, DisbandBtn
 *
 * ASCII wireframe (layout only):
 * +----------------------------------------------------------+
 * | Header (TopBar)                                          |
 * +----------------------------------------------------------+
 * |                                                          |
 * |  +----------------------------------------------------+  |
 * |  | StateContainer (column, centered)                  |  |
 * |  |                                                    |  |
 * |  |  +------------------------------+                  |  |
 * |  |  | IdleContainer (column)       |                  |  |
 * |  |  | [StatusLabel]                |                  |  |
 * |  |  | [CreateBtn]                  |                  |  |
 * |  |  | [JoinBtn]                    |                  |  |
 * |  |  +------------------------------+                  |  |
 * |  |                                                    |  |
 * |  |  +------------------------------+                  |  |
 * |  |  | ActiveContainer (column)     |                  |  |
 * |  |  | [StatusLabel]                |                  |  |
 * |  |  | [ViewMapBtn]                 |                  |  |
 * |  |  | [ShareBtn]                   |                  |  |
 * |  |  | [LeaveBtn]                   |                  |  |
 * |  |  | [DisbandBtn]                 |                  |  |
 * |  |  +------------------------------+                  |  |
 * |  +----------------------------------------------------+  |
 * |                                                          |
 * +----------------------------------------------------------+ */

/**
 * @file team_page_layout.cpp
 * @brief Team page layout
 */

#include "team_page_layout.h"
#include "team_state.h"

namespace team
{
namespace ui
{
namespace layout
{

namespace
{
constexpr int kButtonWidth = 220;
constexpr int kButtonHeight = 32;

void make_non_scrollable(lv_obj_t* obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}
} // namespace

lv_obj_t* create_root(lv_obj_t* parent)
{
    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    make_non_scrollable(root);
    return root;
}

lv_obj_t* create_header(lv_obj_t* root)
{
    lv_obj_t* header = lv_obj_create(root);
    lv_obj_set_size(header, LV_PCT(100), ::ui::widgets::kTopBarHeight);
    make_non_scrollable(header);
    return header;
}

lv_obj_t* create_content(lv_obj_t* root)
{
    lv_obj_t* content = lv_obj_create(root);
    lv_obj_set_size(content, LV_PCT(100), 0);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    make_non_scrollable(content);
    return content;
}

lv_obj_t* create_state_container(lv_obj_t* content)
{
    lv_obj_t* container = lv_obj_create(content);
    lv_obj_set_size(container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    make_non_scrollable(container);
    return container;
}

lv_obj_t* create_idle_container(lv_obj_t* parent)
{
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_set_width(container, LV_PCT(100));
    lv_obj_set_height(container, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    make_non_scrollable(container);

    g_team_state.idle_status_label = lv_label_create(container);
    g_team_state.create_btn = lv_btn_create(container);
    g_team_state.join_btn = lv_btn_create(container);

    lv_obj_set_width(g_team_state.idle_status_label, kButtonWidth);
    lv_obj_set_width(g_team_state.create_btn, kButtonWidth);
    lv_obj_set_width(g_team_state.join_btn, kButtonWidth);
    lv_obj_set_height(g_team_state.create_btn, kButtonHeight);
    lv_obj_set_height(g_team_state.join_btn, kButtonHeight);

    return container;
}

lv_obj_t* create_active_container(lv_obj_t* parent)
{
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_set_width(container, LV_PCT(100));
    lv_obj_set_height(container, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    make_non_scrollable(container);

    g_team_state.active_status_label = lv_label_create(container);
    g_team_state.share_btn = lv_btn_create(container);
    g_team_state.leave_btn = lv_btn_create(container);
    g_team_state.disband_btn = lv_btn_create(container);

    lv_obj_set_width(g_team_state.active_status_label, kButtonWidth);
    lv_obj_set_width(g_team_state.share_btn, kButtonWidth);
    lv_obj_set_width(g_team_state.leave_btn, kButtonWidth);
    lv_obj_set_width(g_team_state.disband_btn, kButtonWidth);
    lv_obj_set_height(g_team_state.share_btn, kButtonHeight);
    lv_obj_set_height(g_team_state.leave_btn, kButtonHeight);
    lv_obj_set_height(g_team_state.disband_btn, kButtonHeight);

    return container;
}

} // namespace layout
} // namespace ui
} // namespace team


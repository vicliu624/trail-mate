/**
 * UI Wireframe / Layout Tree
 * --------------------------------------------------------------------
 *
 * Root (COLUMN)
 * - Header (TopBar)
 * - Content (COLUMN, flex-grow = 1)
 *   - Body (flex-grow = 1)
 *   - Actions (row)
 *
 * Tree view:
 * Root(COL)
 * - Header
 * - Content(COL)
 *   - Body(COL)
 *   - Actions(ROW)
 *
 * ASCII wireframe (layout only):
 * +----------------------------------------------------------+
 * | Header (TopBar)                                          |
 * +----------------------------------------------------------+
 * |                                                          |
 * |  +----------------------------------------------------+  |
 * |  | Body (column, scrollable if needed)                |  |
 * |  +----------------------------------------------------+  |
 * |  | Actions (row)                                      |  |
 * |  | [Action 1]    [Action 2]    [Action 3]             |  |
 * |  +----------------------------------------------------+  |
 * |                                                          |
 * +----------------------------------------------------------+ */

/**
 * @file team_page_layout.cpp
 * @brief Team page layout
 */

#include "ui/screens/team/team_page_layout.h"
#include "ui/page/page_profile.h"
#include "ui/widgets/top_bar.h"

namespace team
{
namespace ui
{
namespace layout
{

namespace
{
void make_non_scrollable(lv_obj_t* obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

lv_coord_t content_row_gap()
{
    const auto& profile = ::ui::page_profile::current();
    return profile.top_content_gap > 0 ? profile.top_content_gap : 3;
}

lv_coord_t actions_height()
{
    const auto& profile = ::ui::page_profile::current();
    const lv_coord_t button_height =
        profile.large_touch_hitbox ? profile.list_item_height : ::ui::page_profile::resolve_control_button_height();
    return button_height + 8;
}
} // namespace

lv_obj_t* create_root(lv_obj_t* parent)
{
    const auto& profile = ::ui::page_profile::current();
    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, profile.top_content_gap, 0);
    make_non_scrollable(root);
    return root;
}

lv_obj_t* create_header(lv_obj_t* root)
{
    const auto& profile = ::ui::page_profile::current();
    lv_obj_t* header = lv_obj_create(root);
    lv_obj_set_size(header, LV_PCT(100), profile.top_bar_height);
    make_non_scrollable(header);
    return header;
}

lv_obj_t* create_content(lv_obj_t* root)
{
    const auto& profile = ::ui::page_profile::current();
    lv_obj_t* content = lv_obj_create(root);
    lv_obj_set_size(content, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(content, profile.content_pad_left, 0);
    lv_obj_set_style_pad_top(content, profile.content_pad_top, 0);
    lv_obj_set_style_pad_bottom(content, profile.content_pad_bottom, 0);
    lv_obj_set_style_pad_left(content, profile.content_pad_left, 0);
    lv_obj_set_style_pad_right(content, profile.content_pad_right, 0);
    lv_obj_set_style_pad_row(content, content_row_gap(), 0);
    make_non_scrollable(content);
    return content;
}

lv_obj_t* create_body(lv_obj_t* content)
{
    lv_obj_t* body = lv_obj_create(content);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    return body;
}

lv_obj_t* create_actions(lv_obj_t* content)
{
    lv_obj_t* actions = lv_obj_create(content);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, actions_height());
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_non_scrollable(actions);
    return actions;
}

} // namespace layout
} // namespace ui
} // namespace team

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
#include "ui/presentation/service_panel_layout.h"
#include "ui/widgets/top_bar.h"

namespace team
{
namespace ui
{
namespace layout
{
namespace service_panel_layout = ::ui::presentation::service_panel_layout;

namespace
{
} // namespace

lv_obj_t* create_root(lv_obj_t* parent)
{
    service_panel_layout::RootSpec spec{};
    spec.pad_row = ::ui::page_profile::current().top_content_gap;
    return service_panel_layout::create_root(parent, spec);
}

lv_obj_t* create_header(lv_obj_t* root)
{
    service_panel_layout::HeaderSpec spec{};
    spec.height = ::ui::page_profile::current().top_bar_height;
    return service_panel_layout::create_header_container(root, spec);
}

lv_obj_t* create_content(lv_obj_t* root)
{
    const auto& profile = ::ui::page_profile::current();
    service_panel_layout::BodySpec spec{};
    spec.pad_row = profile.top_content_gap;
    lv_obj_t* content = service_panel_layout::create_body(root, spec);
    lv_obj_set_style_pad_left(content, profile.content_pad_left, 0);
    lv_obj_set_style_pad_right(content, profile.content_pad_right, 0);
    lv_obj_set_style_pad_top(content, profile.content_pad_top, 0);
    lv_obj_set_style_pad_bottom(content, profile.content_pad_bottom, 0);
    return content;
}

lv_obj_t* create_body(lv_obj_t* content)
{
    return service_panel_layout::create_primary_panel(content);
}

lv_obj_t* create_actions(lv_obj_t* content)
{
    const auto& profile = ::ui::page_profile::current();
    lv_coord_t action_height = profile.list_item_height;
    const lv_coord_t control_height = ::ui::page_profile::resolve_control_button_height();
    if (control_height > action_height)
    {
        action_height = control_height;
    }
    service_panel_layout::FooterActionsSpec spec{};
    spec.height = action_height + 8;
    return service_panel_layout::create_footer_actions(content, spec);
}

} // namespace layout
} // namespace ui
} // namespace team

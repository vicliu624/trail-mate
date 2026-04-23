/**
 * Tracker directory-browser layout:
 * Root(COL) -> Header + Body(ROW)
 * Body -> Selector Panel | Content Panel
 * Content Panel -> Status Label + List Container + Bottom Bar
 */

#include "ui/screens/tracker/tracker_page_layout.h"
#include "ui/page/page_profile.h"
#include "ui/presentation/directory_browser_layout.h"
#include "ui/ui_theme.h"
#include "ui/widgets/top_bar.h"

namespace tracker
{
namespace ui
{
namespace layout
{

namespace
{
const ::ui::page_profile::PageLayoutProfile& page_profile()
{
    return ::ui::page_profile::current();
}

lv_coord_t panel_gap()
{
    return page_profile().large_touch_hitbox ? 6 : 3;
}

lv_coord_t panel_pad()
{
    return page_profile().large_touch_hitbox ? 4 : 3;
}

inline void make_non_scrollable(lv_obj_t* obj)
{
    ::ui::presentation::directory_browser_layout::make_non_scrollable(obj);
}

inline void apply_base_container_style(lv_obj_t* obj)
{
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    make_non_scrollable(obj);
}
} // namespace

lv_obj_t* create_root(lv_obj_t* parent)
{
    const auto& profile = page_profile();
    ::ui::presentation::directory_browser_layout::RootSpec spec;
    spec.pad_row = profile.top_content_gap;
    return ::ui::presentation::directory_browser_layout::create_root(parent, spec);
}

lv_obj_t* create_header(lv_obj_t* root)
{
    const auto& profile = page_profile();
    ::ui::presentation::directory_browser_layout::HeaderSpec spec;
    spec.height = profile.top_bar_height;
    spec.bg_hex = lv_color_to_u32(::ui::theme::page_bg());
    spec.pad_all = 0;
    return ::ui::presentation::directory_browser_layout::create_header_container(root, spec);
}

lv_obj_t* create_content(lv_obj_t* root)
{
    const auto& profile = page_profile();
    ::ui::presentation::directory_browser_layout::BodySpec spec;
    spec.pad_left = profile.content_pad_left;
    spec.pad_right = profile.content_pad_right;
    spec.pad_top = profile.content_pad_top;
    spec.pad_bottom = profile.content_pad_bottom;
    return ::ui::presentation::directory_browser_layout::create_body(root, spec);
}

lv_obj_t* create_filter_panel(lv_obj_t* content, int width)
{
    const auto& profile = page_profile();
    ::ui::presentation::directory_browser_layout::SelectorPanelSpec spec;
    spec.width = width;
    spec.pad_all = panel_pad();
    spec.pad_row = profile.filter_panel_pad_row;
    spec.margin_left = 0;
    spec.margin_right = panel_gap();
    spec.bg_opa = LV_OPA_COVER;
    spec.bg_hex = lv_color_to_u32(::ui::theme::surface_alt());
    return ::ui::presentation::directory_browser_layout::create_selector_panel(content, spec);
}

lv_obj_t* create_list_panel(lv_obj_t* content)
{
    const auto& profile = page_profile();
    ::ui::presentation::directory_browser_layout::ContentPanelSpec spec;
    spec.pad_left = profile.list_panel_pad_left;
    spec.pad_right = profile.list_panel_pad_right;
    spec.pad_top = panel_pad();
    spec.pad_bottom = panel_pad();
    spec.pad_row = profile.list_panel_pad_row;
    spec.margin_left = 0;
    spec.margin_right = 0;
    spec.bg_opa = LV_OPA_COVER;
    spec.bg_hex = lv_color_to_u32(::ui::theme::surface());
    return ::ui::presentation::directory_browser_layout::create_content_panel(content, spec);
}

lv_obj_t* create_list_container(lv_obj_t* list_panel)
{
    const auto& profile = page_profile();
    lv_obj_t* container = lv_obj_create(list_panel);
    lv_obj_set_width(container, LV_PCT(100));
    lv_obj_set_height(container, 0);
    lv_obj_set_flex_grow(container, 1);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(container, profile.list_panel_pad_row, LV_PART_MAIN);
    lv_obj_set_style_pad_left(container, profile.list_panel_pad_left, LV_PART_MAIN);
    lv_obj_set_style_pad_right(container, profile.list_panel_pad_right, LV_PART_MAIN);
    lv_obj_set_style_pad_top(container, panel_pad(), LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(container, panel_pad(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(container, ::ui::theme::surface(), 0);
    apply_base_container_style(container);
    return container;
}

lv_obj_t* create_bottom_bar(lv_obj_t* list_panel)
{
    const auto& profile = page_profile();
    lv_obj_t* bar = lv_obj_create(list_panel);
    lv_obj_set_size(bar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bar, panel_gap(), LV_PART_MAIN);
    lv_obj_set_style_pad_left(bar, profile.list_panel_pad_left, LV_PART_MAIN);
    lv_obj_set_style_pad_right(bar, profile.list_panel_pad_right, LV_PART_MAIN);
    lv_obj_set_style_pad_top(bar, panel_pad(), LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(bar, profile.list_panel_margin_bottom, LV_PART_MAIN);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(bar, ::ui::theme::surface(), 0);
    apply_base_container_style(bar);
    return bar;
}

} // namespace layout
} // namespace ui
} // namespace tracker

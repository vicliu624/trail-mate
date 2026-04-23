#include "ui/presentation/menu_dashboard_layout.h"

#include "ui/presentation/presentation_registry.h"
#include "ui/theme/theme_component_style.h"

namespace ui::presentation::menu_dashboard_layout
{
namespace
{

void apply_component_profile(lv_obj_t* obj, ::ui::theme::ComponentSlot slot)
{
    ::ui::theme::ComponentProfile profile{};
    if (::ui::theme::resolve_component_profile(slot, profile))
    {
        ::ui::theme::apply_component_profile_to_obj(obj, profile);
    }
}

lv_align_t to_lv_align(SchemaAlign align)
{
    switch (align)
    {
    case SchemaAlign::Start:
        return LV_ALIGN_TOP_LEFT;
    case SchemaAlign::End:
        return LV_ALIGN_TOP_RIGHT;
    case SchemaAlign::Center:
    default:
        return LV_ALIGN_TOP_MID;
    }
}

void apply_app_grid_schema(AppGridSpec& spec)
{
    MenuDashboardSchema schema{};
    if (!resolve_active_menu_dashboard_schema(schema))
    {
        return;
    }

    if (schema.has_app_grid_height_pct)
    {
        spec.height_pct = schema.app_grid_height_pct;
    }
    if (schema.has_app_grid_top_offset)
    {
        spec.top_offset = schema.app_grid_top_offset;
    }
    if (schema.has_app_grid_pad_row)
    {
        spec.pad_row = schema.app_grid_pad_row;
    }
    if (schema.has_app_grid_pad_column)
    {
        spec.pad_column = schema.app_grid_pad_column;
    }
    if (schema.has_app_grid_pad_left)
    {
        spec.pad_left = schema.app_grid_pad_left;
    }
    if (schema.has_app_grid_pad_right)
    {
        spec.pad_right = schema.app_grid_pad_right;
    }
    if (schema.has_app_grid_align)
    {
        spec.align = to_lv_align(schema.app_grid_align);
    }
}

void apply_bottom_chips_schema(BottomChipsSpec& spec)
{
    MenuDashboardSchema schema{};
    if (!resolve_active_menu_dashboard_schema(schema))
    {
        return;
    }

    if (schema.has_bottom_chips_height)
    {
        spec.height = schema.bottom_chips_height;
    }
    if (schema.has_bottom_chips_pad_left)
    {
        spec.pad_left = schema.bottom_chips_pad_left;
    }
    if (schema.has_bottom_chips_pad_right)
    {
        spec.pad_right = schema.bottom_chips_pad_right;
    }
    if (schema.has_bottom_chips_pad_column)
    {
        spec.pad_column = schema.bottom_chips_pad_column;
    }
}

} // namespace

void make_non_scrollable(lv_obj_t* obj)
{
    if (!obj)
    {
        return;
    }
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

lv_obj_t* create_app_grid_region(lv_obj_t* parent, const AppGridSpec& spec)
{
    AppGridSpec resolved = spec;
    apply_app_grid_schema(resolved);

    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_size(panel, resolved.width, LV_PCT(resolved.height_pct));
    lv_obj_set_style_pad_top(panel, 0, 0);
    lv_obj_set_style_pad_bottom(panel, 0, 0);
    lv_obj_set_style_pad_left(panel, resolved.pad_left, 0);
    lv_obj_set_style_pad_right(panel, resolved.pad_right, 0);
    lv_obj_set_style_pad_row(panel, resolved.pad_row, 0);
    lv_obj_set_style_pad_column(panel, resolved.pad_column, 0);
    lv_obj_set_flex_flow(panel, resolved.flow);
    lv_obj_set_flex_align(panel,
                          resolved.main_align,
                          resolved.cross_align,
                          resolved.track_align);
    lv_obj_set_scroll_dir(panel, resolved.scroll_dir);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_shadow_width(panel, 0, 0);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
    apply_component_profile(panel, ::ui::theme::ComponentSlot::MenuDashboardAppGrid);
    if (resolved.snap_x != LV_SCROLL_SNAP_NONE)
    {
        lv_obj_set_scroll_snap_x(panel, resolved.snap_x);
    }
    lv_obj_align(panel, resolved.align, 0, resolved.top_offset);
    return panel;
}

lv_obj_t* create_app_list_region(lv_obj_t* parent, const AppListSpec& spec)
{
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_size(panel, spec.width, LV_PCT(100));
    lv_obj_set_style_pad_left(panel, spec.pad_left, 0);
    lv_obj_set_style_pad_right(panel, spec.pad_right, 0);
    lv_obj_set_style_pad_top(panel, spec.pad_top, 0);
    lv_obj_set_style_pad_bottom(panel, spec.pad_bottom, 0);
    lv_obj_set_style_pad_row(panel, spec.pad_row, 0);
    lv_obj_set_style_pad_column(panel, 0, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_shadow_width(panel, 0, 0);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_AUTO);
    apply_component_profile(panel, ::ui::theme::ComponentSlot::MenuDashboardAppGrid);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, spec.top_offset);
    return panel;
}

lv_obj_t* create_bottom_chips_region(lv_obj_t* parent, const BottomChipsSpec& spec)
{
    BottomChipsSpec resolved = spec;
    apply_bottom_chips_schema(resolved);

    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), resolved.height);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(bar, resolved.pad_left, 0);
    lv_obj_set_style_pad_right(bar, resolved.pad_right, 0);
    lv_obj_set_style_pad_top(bar, resolved.pad_top, 0);
    lv_obj_set_style_pad_bottom(bar, resolved.pad_bottom, 0);
    lv_obj_set_style_pad_column(bar, resolved.pad_column, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_shadow_width(bar, 0, 0);
    make_non_scrollable(bar);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    apply_component_profile(bar, ::ui::theme::ComponentSlot::MenuDashboardBottomChips);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    return bar;
}

} // namespace ui::presentation::menu_dashboard_layout

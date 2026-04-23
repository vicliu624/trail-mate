#include "ui/presentation/map_focus_layout.h"

#include "ui/page/page_profile.h"
#include "ui/presentation/presentation_registry.h"
#include "ui/theme/theme_component_style.h"

namespace ui::presentation::map_focus_layout
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

lv_coord_t default_header_height()
{
    const auto& profile = ::ui::page_profile::current();
    return profile.top_bar_height > 0 ? profile.top_bar_height : 28;
}

void apply_base_container_style(lv_obj_t* obj)
{
    if (!obj)
    {
        return;
    }
    lv_obj_set_style_border_width(obj, 0, 0);
}

void apply_background(lv_obj_t* obj, lv_opa_t opa, std::uint32_t hex)
{
    if (!obj)
    {
        return;
    }
    lv_obj_set_style_bg_opa(obj, opa, 0);
    if (opa != LV_OPA_TRANSP)
    {
        lv_obj_set_style_bg_color(obj, lv_color_hex(hex), 0);
    }
}

lv_align_t to_lv_align(SchemaCorner corner)
{
    switch (corner)
    {
    case SchemaCorner::TopLeft:
        return LV_ALIGN_TOP_LEFT;
    case SchemaCorner::BottomRight:
        return LV_ALIGN_BOTTOM_RIGHT;
    case SchemaCorner::BottomLeft:
        return LV_ALIGN_BOTTOM_LEFT;
    case SchemaCorner::TopRight:
    default:
        return LV_ALIGN_TOP_RIGHT;
    }
}

lv_flex_flow_t to_lv_flow(SchemaFlow flow)
{
    return flow == SchemaFlow::Row ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN;
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

lv_obj_t* create_root(lv_obj_t* parent, const RootSpec& spec)
{
    RootSpec resolved_spec = spec;
    MapFocusSchema schema{};
    if (resolve_active_map_focus_schema(schema) && schema.has_root_pad_row)
    {
        resolved_spec.pad_row = schema.root_pad_row;
    }

    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_set_size(root, resolved_spec.width, resolved_spec.height);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_pad_row(root, resolved_spec.pad_row, 0);
    lv_obj_set_style_radius(root, resolved_spec.radius, 0);
    apply_base_container_style(root);
    apply_background(root, resolved_spec.bg_opa, resolved_spec.bg_hex);
    apply_component_profile(root, ::ui::theme::ComponentSlot::MapFocusRoot);
    make_non_scrollable(root);
    return root;
}

lv_obj_t* create_header_container(lv_obj_t* parent, const HeaderSpec& spec)
{
    lv_obj_t* header = lv_obj_create(parent);
    lv_obj_set_size(header, LV_PCT(100), spec.height > 0 ? spec.height : default_header_height());
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    apply_base_container_style(header);
    make_non_scrollable(header);
    return header;
}

lv_obj_t* create_content(lv_obj_t* parent, const ContentSpec& spec)
{
    lv_obj_t* content = lv_obj_create(parent);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_height(content, 0);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(content, spec.pad_all, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    apply_base_container_style(content);
    make_non_scrollable(content);
    return content;
}

lv_obj_t* create_viewport_region(lv_obj_t* parent, const ViewportSpec& spec)
{
    lv_obj_t* viewport = lv_obj_create(parent);
    lv_obj_set_size(viewport, spec.width, spec.height);
    lv_obj_set_flex_grow(viewport, spec.grow ? 1 : 0);
    lv_obj_set_style_pad_all(viewport, 0, 0);
    lv_obj_set_style_bg_opa(viewport, LV_OPA_TRANSP, 0);
    apply_base_container_style(viewport);
    make_non_scrollable(viewport);
    if (spec.clickable)
    {
        lv_obj_add_flag(viewport, LV_OBJ_FLAG_CLICKABLE);
    }
    return viewport;
}

lv_obj_t* create_overlay_panel(lv_obj_t* parent,
                               OverlayPanelRole role,
                               const OverlayPanelSpec& spec)
{
    OverlayPanelSpec resolved_spec = spec;
    MapFocusSchema schema{};
    if (resolve_active_map_focus_schema(schema))
    {
        if (role == OverlayPanelRole::Primary)
        {
            if (schema.has_primary_overlay_position)
            {
                resolved_spec.align = to_lv_align(schema.primary_overlay_position);
            }
            if (schema.has_primary_overlay_width)
            {
                resolved_spec.width = schema.primary_overlay_width;
            }
            if (schema.has_primary_overlay_offset_x)
            {
                resolved_spec.align_x = schema.primary_overlay_offset_x;
            }
            if (schema.has_primary_overlay_offset_y)
            {
                resolved_spec.align_y = schema.primary_overlay_offset_y;
            }
            if (schema.has_primary_overlay_gap_row)
            {
                resolved_spec.pad_row = schema.primary_overlay_gap_row;
            }
            if (schema.has_primary_overlay_gap_column)
            {
                resolved_spec.pad_column = schema.primary_overlay_gap_column;
            }
            if (schema.has_primary_overlay_flow)
            {
                resolved_spec.flow = to_lv_flow(schema.primary_overlay_flow);
            }
        }
        else
        {
            if (schema.has_secondary_overlay_position)
            {
                resolved_spec.align = to_lv_align(schema.secondary_overlay_position);
            }
            if (schema.has_secondary_overlay_width)
            {
                resolved_spec.width = schema.secondary_overlay_width;
            }
            if (schema.has_secondary_overlay_offset_x)
            {
                resolved_spec.align_x = schema.secondary_overlay_offset_x;
            }
            if (schema.has_secondary_overlay_offset_y)
            {
                resolved_spec.align_y = schema.secondary_overlay_offset_y;
            }
            if (schema.has_secondary_overlay_gap_row)
            {
                resolved_spec.pad_row = schema.secondary_overlay_gap_row;
            }
            if (schema.has_secondary_overlay_gap_column)
            {
                resolved_spec.pad_column = schema.secondary_overlay_gap_column;
            }
            if (schema.has_secondary_overlay_flow)
            {
                resolved_spec.flow = to_lv_flow(schema.secondary_overlay_flow);
            }
        }
    }

    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_size(panel, resolved_spec.width, resolved_spec.height);
    lv_obj_set_flex_flow(panel, resolved_spec.flow);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(panel, resolved_spec.pad_all, 0);
    lv_obj_set_style_pad_row(panel, resolved_spec.pad_row, 0);
    lv_obj_set_style_pad_column(panel, resolved_spec.pad_column, 0);
    apply_base_container_style(panel);
    apply_background(panel, resolved_spec.bg_opa, resolved_spec.bg_hex);
    apply_component_profile(panel,
                            role == OverlayPanelRole::Primary
                                ? ::ui::theme::ComponentSlot::MapFocusPrimaryOverlay
                                : ::ui::theme::ComponentSlot::MapFocusSecondaryOverlay);
    if (resolved_spec.scrollbar_mode == LV_SCROLLBAR_MODE_OFF)
    {
        make_non_scrollable(panel);
    }
    else
    {
        lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(panel, resolved_spec.scrollbar_mode);
    }
    lv_obj_align(panel, resolved_spec.align, resolved_spec.align_x, resolved_spec.align_y);
    return panel;
}

} // namespace ui::presentation::map_focus_layout

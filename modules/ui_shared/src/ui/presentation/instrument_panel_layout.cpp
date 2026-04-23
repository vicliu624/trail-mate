#include "ui/presentation/instrument_panel_layout.h"

#include "ui/page/page_profile.h"
#include "ui/presentation/presentation_registry.h"
#include "ui/theme/theme_component_style.h"

namespace ui::presentation::instrument_panel_layout
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

lv_flex_flow_t to_lv_flow(SchemaFlow flow)
{
    return flow == SchemaFlow::Column ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW;
}

lv_obj_t* create_region(lv_obj_t* parent, RegionRole role, const RegionSpec& spec)
{
    RegionSpec resolved_spec = spec;
    InstrumentPanelSchema schema{};
    if (resolve_active_instrument_panel_schema(schema))
    {
        if (role == RegionRole::Canvas)
        {
            if (schema.has_canvas_grow)
            {
                resolved_spec.grow = schema.canvas_grow;
            }
            if (schema.has_canvas_pad_all)
            {
                resolved_spec.pad_all = schema.canvas_pad_all;
            }
            if (schema.has_canvas_pad_row)
            {
                resolved_spec.pad_row = schema.canvas_pad_row;
            }
            if (schema.has_canvas_pad_column)
            {
                resolved_spec.pad_column = schema.canvas_pad_column;
            }
        }
        else
        {
            if (schema.has_legend_grow)
            {
                resolved_spec.grow = schema.legend_grow;
            }
            if (schema.has_legend_pad_all)
            {
                resolved_spec.pad_all = schema.legend_pad_all;
            }
            if (schema.has_legend_pad_row)
            {
                resolved_spec.pad_row = schema.legend_pad_row;
            }
            if (schema.has_legend_pad_column)
            {
                resolved_spec.pad_column = schema.legend_pad_column;
            }
        }
    }

    lv_obj_t* region = lv_obj_create(parent);
    lv_obj_set_size(region, resolved_spec.width, resolved_spec.height);
    lv_obj_set_flex_grow(region, resolved_spec.grow ? 1 : 0);
    lv_obj_set_style_pad_all(region, resolved_spec.pad_all, 0);
    lv_obj_set_style_pad_row(region, resolved_spec.pad_row, 0);
    lv_obj_set_style_pad_column(region, resolved_spec.pad_column, 0);
    lv_obj_set_style_margin_left(region, resolved_spec.margin_left, 0);
    lv_obj_set_style_margin_right(region, resolved_spec.margin_right, 0);
    lv_obj_set_style_margin_top(region, resolved_spec.margin_top, 0);
    lv_obj_set_style_margin_bottom(region, resolved_spec.margin_bottom, 0);
    apply_base_container_style(region);
    apply_background(region, resolved_spec.bg_opa, resolved_spec.bg_hex);
    apply_component_profile(region,
                            role == RegionRole::Canvas
                                ? ::ui::theme::ComponentSlot::InstrumentPanelCanvasRegion
                                : ::ui::theme::ComponentSlot::InstrumentPanelLegendRegion);
    if (resolved_spec.scrollbar_mode == LV_SCROLLBAR_MODE_OFF)
    {
        make_non_scrollable(region);
    }
    else
    {
        lv_obj_add_flag(region, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(region, resolved_spec.scrollbar_mode);
    }
    return region;
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
    InstrumentPanelSchema schema{};
    if (resolve_active_instrument_panel_schema(schema) && schema.has_root_pad_row)
    {
        resolved_spec.pad_row = schema.root_pad_row;
    }

    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_set_size(root, resolved_spec.width, resolved_spec.height);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, resolved_spec.pad_row, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_radius(root, resolved_spec.radius, 0);
    apply_base_container_style(root);
    apply_background(root, resolved_spec.bg_opa, resolved_spec.bg_hex);
    apply_component_profile(root, ::ui::theme::ComponentSlot::InstrumentPanelRoot);
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

lv_obj_t* create_body(lv_obj_t* parent, const BodySpec& spec)
{
    BodySpec resolved_spec = spec;
    InstrumentPanelSchema schema{};
    if (resolve_active_instrument_panel_schema(schema))
    {
        if (schema.has_body_flow)
        {
            resolved_spec.flow = to_lv_flow(schema.body_flow);
        }
        if (schema.has_body_pad_all)
        {
            resolved_spec.pad_all = schema.body_pad_all;
        }
        if (schema.has_body_pad_row)
        {
            resolved_spec.pad_row = schema.body_pad_row;
        }
        if (schema.has_body_pad_column)
        {
            resolved_spec.pad_column = schema.body_pad_column;
        }
    }

    lv_obj_t* body = lv_obj_create(parent);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_height(body, 0);
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, resolved_spec.flow);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(body, resolved_spec.pad_all, 0);
    lv_obj_set_style_pad_row(body, resolved_spec.pad_row, 0);
    lv_obj_set_style_pad_column(body, resolved_spec.pad_column, 0);
    apply_base_container_style(body);
    apply_component_profile(body, ::ui::theme::ComponentSlot::InstrumentPanelBody);
    make_non_scrollable(body);
    return body;
}

lv_obj_t* create_canvas_region(lv_obj_t* parent, const RegionSpec& spec)
{
    return create_region(parent, RegionRole::Canvas, spec);
}

lv_obj_t* create_legend_region(lv_obj_t* parent, const RegionSpec& spec)
{
    return create_region(parent, RegionRole::Legend, spec);
}

} // namespace ui::presentation::instrument_panel_layout

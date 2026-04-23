#include "ui/presentation/service_panel_layout.h"

#include "ui/page/page_profile.h"
#include "ui/presentation/presentation_registry.h"
#include "ui/theme/theme_component_style.h"

namespace ui::presentation::service_panel_layout
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
    lv_obj_set_style_radius(obj, 0, 0);
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

void apply_scroll_behavior(lv_obj_t* obj, lv_scrollbar_mode_t mode)
{
    if (!obj)
    {
        return;
    }
    if (mode == LV_SCROLLBAR_MODE_OFF)
    {
        make_non_scrollable(obj);
        return;
    }
    lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, mode);
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
    ServicePanelSchema schema{};
    if (resolve_active_service_panel_schema(schema) && schema.has_root_pad_row)
    {
        resolved_spec.pad_row = schema.root_pad_row;
    }

    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_set_size(root, resolved_spec.width, resolved_spec.height);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_pad_row(root, resolved_spec.pad_row, 0);
    apply_background(root, resolved_spec.bg_opa, resolved_spec.bg_hex);
    apply_base_container_style(root);
    apply_component_profile(root, ::ui::theme::ComponentSlot::ServicePanelRoot);
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
    ServicePanelSchema schema{};
    if (resolve_active_service_panel_schema(schema))
    {
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
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(body, resolved_spec.pad_all, 0);
    lv_obj_set_style_pad_row(body, resolved_spec.pad_row, 0);
    lv_obj_set_style_pad_column(body, resolved_spec.pad_column, 0);
    apply_base_container_style(body);
    apply_component_profile(body, ::ui::theme::ComponentSlot::ServicePanelBody);
    make_non_scrollable(body);
    return body;
}

lv_obj_t* create_primary_panel(lv_obj_t* parent, const PrimaryPanelSpec& spec)
{
    PrimaryPanelSpec resolved_spec = spec;
    ServicePanelSchema schema{};
    if (resolve_active_service_panel_schema(schema))
    {
        if (schema.has_primary_panel_pad_all)
        {
            resolved_spec.pad_all = schema.primary_panel_pad_all;
        }
        if (schema.has_primary_panel_pad_row)
        {
            resolved_spec.pad_row = schema.primary_panel_pad_row;
        }
        if (schema.has_primary_panel_pad_column)
        {
            resolved_spec.pad_column = schema.primary_panel_pad_column;
        }
    }

    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_width(panel, LV_PCT(100));
    lv_obj_set_height(panel, 0);
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(panel, resolved_spec.pad_all, 0);
    lv_obj_set_style_pad_row(panel, resolved_spec.pad_row, 0);
    lv_obj_set_style_pad_column(panel, resolved_spec.pad_column, 0);
    apply_base_container_style(panel);
    apply_background(panel, resolved_spec.bg_opa, resolved_spec.bg_hex);
    apply_component_profile(panel, ::ui::theme::ComponentSlot::ServicePanelPrimaryPanel);
    apply_scroll_behavior(panel, resolved_spec.scrollbar_mode);
    return panel;
}

lv_obj_t* create_center_stack(lv_obj_t* parent, const CenterStackSpec& spec)
{
    lv_obj_t* stack = lv_obj_create(parent);
    lv_obj_set_width(stack, spec.width);
    lv_obj_set_height(stack, 0);
    lv_obj_set_flex_grow(stack, 1);
    lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(stack, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(stack, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(stack, spec.pad_all, 0);
    lv_obj_set_style_pad_row(stack, spec.pad_row, 0);
    apply_base_container_style(stack);
    make_non_scrollable(stack);
    return stack;
}

lv_obj_t* create_footer_actions(lv_obj_t* parent, const FooterActionsSpec& spec)
{
    FooterActionsSpec resolved_spec = spec;
    ServicePanelSchema schema{};
    if (resolve_active_service_panel_schema(schema))
    {
        if (schema.has_footer_actions_height)
        {
            resolved_spec.height = schema.footer_actions_height;
        }
        if (schema.has_footer_actions_pad_all)
        {
            resolved_spec.pad_all = schema.footer_actions_pad_all;
        }
        if (schema.has_footer_actions_pad_row)
        {
            resolved_spec.pad_row = schema.footer_actions_pad_row;
        }
        if (schema.has_footer_actions_pad_column)
        {
            resolved_spec.pad_column = schema.footer_actions_pad_column;
        }
    }

    lv_obj_t* footer = lv_obj_create(parent);
    lv_obj_set_width(footer, LV_PCT(100));
    if (resolved_spec.height != LV_SIZE_CONTENT)
    {
        lv_obj_set_height(footer, resolved_spec.height);
    }
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(footer, resolved_spec.pad_all, 0);
    lv_obj_set_style_pad_row(footer, resolved_spec.pad_row, 0);
    lv_obj_set_style_pad_column(footer, resolved_spec.pad_column, 0);
    apply_base_container_style(footer);
    apply_component_profile(footer, ::ui::theme::ComponentSlot::ServicePanelFooterActions);
    make_non_scrollable(footer);
    return footer;
}

} // namespace ui::presentation::service_panel_layout

#include "ui/presentation/directory_browser_layout.h"

#include "ui/components/two_pane_layout.h"
#include "ui/presentation/presentation_registry.h"

namespace ui::presentation
{
bool directory_browser_uses_stacked_selectors();
}

namespace ui::presentation::directory_browser_layout
{
namespace
{

void apply_base_container_style(lv_obj_t* obj)
{
    if (!obj)
    {
        return;
    }
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
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

void apply_background(lv_obj_t* obj, lv_opa_t bg_opa, std::uint32_t bg_hex)
{
    if (!obj || bg_opa == LV_OPA_TRANSP)
    {
        return;
    }
    lv_obj_set_style_bg_opa(obj, bg_opa, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg_hex), 0);
}

DirectoryBrowserSchema active_schema()
{
    DirectoryBrowserSchema schema{};
    (void)::ui::presentation::resolve_active_directory_browser_schema(schema);
    return schema;
}

bool use_stacked_layout()
{
    const DirectoryBrowserSchema schema = active_schema();
    if (schema.has_axis)
    {
        return schema.axis == DirectoryBrowserAxis::Column;
    }
    return ::ui::presentation::directory_browser_uses_stacked_selectors();
}

bool selector_is_first()
{
    const DirectoryBrowserSchema schema = active_schema();
    return !schema.has_selector_first || schema.selector_first;
}

void apply_selector_panel_schema(SelectorPanelSpec& spec)
{
    const DirectoryBrowserSchema schema = active_schema();
    const bool stacked = use_stacked_layout();
    const bool first = selector_is_first();

    if (schema.has_selector_width)
    {
        spec.width = schema.selector_width;
    }
    if (schema.has_selector_pad_all)
    {
        spec.pad_all = schema.selector_pad_all;
    }
    if (schema.has_selector_pad_row)
    {
        spec.pad_row = schema.selector_pad_row;
    }
    if (schema.has_selector_margin_after)
    {
        if (stacked)
        {
            spec.margin_top = 0;
            spec.margin_bottom = first ? schema.selector_margin_after : 0;
        }
        else
        {
            spec.margin_left = first ? 0 : schema.selector_margin_after;
            spec.margin_right = first ? schema.selector_margin_after : 0;
        }
    }
}

void apply_selector_controls_schema(SelectorControlsSpec& spec)
{
    const DirectoryBrowserSchema schema = active_schema();
    if (schema.has_selector_gap_row)
    {
        spec.pad_row = schema.selector_gap_row;
    }
    if (schema.has_selector_gap_column)
    {
        spec.pad_column = schema.selector_gap_column;
    }
}

void apply_selector_button_schema(SelectorButtonSpec& spec)
{
    const DirectoryBrowserSchema schema = active_schema();
    if (schema.has_selector_button_height)
    {
        spec.height = schema.selector_button_height;
    }
    if (schema.has_selector_button_stacked_min_width)
    {
        spec.stacked_min_width = schema.selector_button_stacked_min_width;
    }
    if (schema.has_selector_button_stacked_flex_grow)
    {
        spec.stacked_flex_grow = schema.selector_button_stacked_flex_grow;
    }
}

void apply_content_panel_schema(ContentPanelSpec& spec)
{
    const DirectoryBrowserSchema schema = active_schema();
    if (schema.has_content_pad_all)
    {
        spec.pad_all = schema.content_pad_all;
    }
    if (schema.has_content_pad_row)
    {
        spec.pad_row = schema.content_pad_row;
    }
    if (schema.has_content_pad_left)
    {
        spec.pad_left = schema.content_pad_left;
    }
    if (schema.has_content_pad_right)
    {
        spec.pad_right = schema.content_pad_right;
    }
    if (schema.has_content_pad_top)
    {
        spec.pad_top = schema.content_pad_top;
    }
    if (schema.has_content_pad_bottom)
    {
        spec.pad_bottom = schema.content_pad_bottom;
    }
}

} // namespace

void make_non_scrollable(lv_obj_t* obj)
{
    ::ui::components::two_pane_layout::make_non_scrollable(obj);
}

lv_obj_t* create_root(lv_obj_t* parent, const RootSpec& spec)
{
    ::ui::components::two_pane_layout::RootSpec legacy_spec;
    legacy_spec.pad_row = spec.pad_row;
    return ::ui::components::two_pane_layout::create_root(parent, legacy_spec);
}

lv_obj_t* create_header_container(lv_obj_t* parent, const HeaderSpec& spec)
{
    ::ui::components::two_pane_layout::HeaderSpec legacy_spec;
    legacy_spec.height = spec.height;
    legacy_spec.bg_hex = spec.bg_hex;
    legacy_spec.pad_all = spec.pad_all;
    return ::ui::components::two_pane_layout::create_header_container(parent, legacy_spec);
}

lv_obj_t* create_body(lv_obj_t* parent, const BodySpec& spec)
{
    if (use_stacked_layout())
    {
        lv_obj_t* body = lv_obj_create(parent);
        lv_obj_set_width(body, LV_PCT(100));
        lv_obj_set_height(body, 0);
        lv_obj_set_flex_grow(body, 1);
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(body,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);
        lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_left(body, spec.pad_left, 0);
        lv_obj_set_style_pad_right(body, spec.pad_right, 0);
        lv_obj_set_style_pad_top(body, spec.pad_top, 0);
        lv_obj_set_style_pad_bottom(body, spec.pad_bottom, 0);
        apply_base_container_style(body);
        make_non_scrollable(body);
        return body;
    }

    ::ui::components::two_pane_layout::ContentSpec legacy_spec;
    legacy_spec.pad_left = spec.pad_left;
    legacy_spec.pad_right = spec.pad_right;
    legacy_spec.pad_top = spec.pad_top;
    legacy_spec.pad_bottom = spec.pad_bottom;
    return ::ui::components::two_pane_layout::create_content_row(parent, legacy_spec);
}

lv_obj_t* create_selector_panel(lv_obj_t* parent, const SelectorPanelSpec& spec)
{
    SelectorPanelSpec resolved = spec;
    apply_selector_panel_schema(resolved);

    if (use_stacked_layout())
    {
        lv_obj_t* panel = lv_obj_create(parent);
        lv_obj_set_width(panel, LV_PCT(100));
        lv_obj_set_height(panel, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(panel,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_all(panel, resolved.pad_all >= 0 ? resolved.pad_all : 0, LV_PART_MAIN);
        lv_obj_set_style_pad_row(panel, resolved.pad_row, LV_PART_MAIN);
        lv_obj_set_style_margin_left(panel, resolved.margin_left, LV_PART_MAIN);
        lv_obj_set_style_margin_right(panel, resolved.margin_right, LV_PART_MAIN);
        lv_obj_set_style_margin_top(panel, resolved.margin_top, LV_PART_MAIN);
        lv_obj_set_style_margin_bottom(panel, resolved.margin_bottom, LV_PART_MAIN);
        apply_base_container_style(panel);
        apply_scroll_behavior(panel, resolved.scrollbar_mode);
        apply_background(panel, resolved.bg_opa, resolved.bg_hex);
        if (selector_is_first())
        {
            lv_obj_move_to_index(panel, 0);
        }
        return panel;
    }

    ::ui::components::two_pane_layout::SidePanelSpec legacy_spec;
    legacy_spec.width = resolved.width;
    legacy_spec.pad_row = resolved.pad_row;
    legacy_spec.margin_left = resolved.margin_left;
    legacy_spec.margin_right = resolved.margin_right;
    legacy_spec.scrollbar_mode = resolved.scrollbar_mode;

    lv_obj_t* panel = ::ui::components::two_pane_layout::create_side_panel(parent, legacy_spec);
    if (resolved.pad_all >= 0)
    {
        lv_obj_set_style_pad_all(panel, resolved.pad_all, LV_PART_MAIN);
    }
    lv_obj_set_style_margin_top(panel, resolved.margin_top, LV_PART_MAIN);
    lv_obj_set_style_margin_bottom(panel, resolved.margin_bottom, LV_PART_MAIN);
    apply_background(panel, resolved.bg_opa, resolved.bg_hex);
    if (selector_is_first())
    {
        lv_obj_move_to_index(panel, 0);
    }
    return panel;
}

lv_obj_t* create_selector_controls(lv_obj_t* parent, const SelectorControlsSpec& spec)
{
    SelectorControlsSpec resolved = spec;
    apply_selector_controls_schema(resolved);

    lv_obj_t* controls = lv_obj_create(parent);
    lv_obj_set_width(controls, LV_PCT(100));
    lv_obj_set_height(controls, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(controls, resolved.pad_all, 0);
    lv_obj_set_style_pad_row(controls, resolved.pad_row, 0);
    lv_obj_set_style_pad_column(controls, resolved.pad_column, 0);
    lv_obj_set_style_margin_top(controls, resolved.margin_top, LV_PART_MAIN);
    lv_obj_set_style_margin_bottom(controls, resolved.margin_bottom, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(controls, LV_OPA_TRANSP, 0);
    apply_base_container_style(controls);
    make_non_scrollable(controls);

    if (use_stacked_layout())
    {
        lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(controls,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);
    }
    else
    {
        lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(controls,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);
    }

    return controls;
}

void configure_selector_button(lv_obj_t* button, const SelectorButtonSpec& spec)
{
    if (!button)
    {
        return;
    }

    SelectorButtonSpec resolved = spec;
    apply_selector_button_schema(resolved);

    lv_obj_set_height(button, resolved.height);
    make_non_scrollable(button);

    if (use_stacked_layout())
    {
        lv_obj_set_width(button, LV_SIZE_CONTENT);
        lv_obj_set_style_min_width(button, resolved.stacked_min_width, 0);
        lv_obj_set_flex_grow(button, resolved.stacked_flex_grow ? 1 : 0);
    }
    else
    {
        lv_obj_set_width(button, resolved.split_width);
        lv_obj_set_style_min_width(button, 0, 0);
        lv_obj_set_flex_grow(button, 0);
    }
}

lv_obj_t* create_content_panel(lv_obj_t* parent, const ContentPanelSpec& spec)
{
    ContentPanelSpec resolved = spec;
    apply_content_panel_schema(resolved);

    if (use_stacked_layout())
    {
        lv_obj_t* panel = lv_obj_create(parent);
        lv_obj_set_width(panel, LV_PCT(100));
        lv_obj_set_height(panel, 0);
        lv_obj_set_flex_grow(panel, 1);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(panel,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_all(panel, resolved.pad_all, 0);
        lv_obj_set_style_pad_row(panel, resolved.pad_row, 0);
        lv_obj_set_style_pad_left(panel, resolved.pad_left, 0);
        lv_obj_set_style_pad_right(panel, resolved.pad_right, 0);
        lv_obj_set_style_pad_top(panel, resolved.pad_top, 0);
        lv_obj_set_style_pad_bottom(panel, resolved.pad_bottom, 0);
        lv_obj_set_style_margin_left(panel, resolved.margin_left, LV_PART_MAIN);
        lv_obj_set_style_margin_right(panel, resolved.margin_right, LV_PART_MAIN);
        lv_obj_set_style_margin_bottom(panel, resolved.margin_bottom, LV_PART_MAIN);
        apply_base_container_style(panel);
        apply_scroll_behavior(panel, resolved.scrollbar_mode);
        apply_background(panel, resolved.bg_opa, resolved.bg_hex);
        if (!selector_is_first())
        {
            lv_obj_move_to_index(panel, 0);
        }
        return panel;
    }

    ::ui::components::two_pane_layout::MainPanelSpec legacy_spec;
    legacy_spec.pad_all = resolved.pad_all;
    legacy_spec.pad_row = resolved.pad_row;
    legacy_spec.pad_left = resolved.pad_left;
    legacy_spec.pad_right = resolved.pad_right;
    legacy_spec.pad_top = resolved.pad_top;
    legacy_spec.pad_bottom = resolved.pad_bottom;
    legacy_spec.margin_left = resolved.margin_left;
    legacy_spec.margin_right = resolved.margin_right;
    legacy_spec.margin_bottom = resolved.margin_bottom;
    legacy_spec.scrollbar_mode = resolved.scrollbar_mode;

    lv_obj_t* panel = ::ui::components::two_pane_layout::create_main_panel(parent, legacy_spec);
    apply_background(panel, resolved.bg_opa, resolved.bg_hex);
    if (!selector_is_first())
    {
        lv_obj_move_to_index(panel, 0);
    }
    return panel;
}

} // namespace ui::presentation::directory_browser_layout

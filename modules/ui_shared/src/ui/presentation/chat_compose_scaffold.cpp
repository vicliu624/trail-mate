#include "ui/presentation/chat_compose_scaffold.h"

#include "ui/page/page_profile.h"
#include "ui/presentation/presentation_registry.h"
#include "ui/theme/theme_component_style.h"

namespace ui::presentation::chat_compose_scaffold
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

ChatComposeSchema active_schema()
{
    ChatComposeSchema schema{};
    (void)::ui::presentation::resolve_active_chat_compose_schema(schema);
    return schema;
}

lv_flex_align_t to_lv_flex_align(SchemaAlign align)
{
    switch (align)
    {
    case SchemaAlign::Center:
        return LV_FLEX_ALIGN_CENTER;
    case SchemaAlign::End:
        return LV_FLEX_ALIGN_END;
    case SchemaAlign::Start:
    default:
        return LV_FLEX_ALIGN_START;
    }
}

void apply_root_schema(RootSpec& spec)
{
    const ChatComposeSchema schema = active_schema();
    if (schema.has_root_pad_row)
    {
        spec.pad_row = schema.root_pad_row;
    }
}

void apply_content_schema(ContentSpec& spec)
{
    const ChatComposeSchema schema = active_schema();
    if (schema.has_content_pad_all)
    {
        spec.pad_all = schema.content_pad_all;
    }
    if (schema.has_content_pad_row)
    {
        spec.pad_row = schema.content_pad_row;
    }
}

void apply_action_bar_schema(ActionBarSpec& spec)
{
    const ChatComposeSchema schema = active_schema();
    if (schema.has_action_bar_height)
    {
        spec.height = schema.action_bar_height;
    }
    if (schema.has_action_bar_pad_left)
    {
        spec.pad_left = schema.action_bar_pad_left;
    }
    if (schema.has_action_bar_pad_right)
    {
        spec.pad_right = schema.action_bar_pad_right;
    }
    if (schema.has_action_bar_pad_top)
    {
        spec.pad_top = schema.action_bar_pad_top;
    }
    if (schema.has_action_bar_pad_bottom)
    {
        spec.pad_bottom = schema.action_bar_pad_bottom;
    }
    if (schema.has_action_bar_pad_column)
    {
        spec.pad_column = schema.action_bar_pad_column;
    }
    if (schema.has_action_bar_align)
    {
        spec.main_align = to_lv_flex_align(schema.action_bar_align);
    }
}

void apply_button_schema(ActionButtonRole role, ActionButtonSpec& spec)
{
    const ChatComposeSchema schema = active_schema();
    if (role == ActionButtonRole::Primary)
    {
        if (schema.has_primary_button_width)
        {
            spec.width = schema.primary_button_width;
        }
        if (schema.has_primary_button_height)
        {
            spec.height = schema.primary_button_height;
        }
        return;
    }

    if (schema.has_secondary_button_width)
    {
        spec.width = schema.secondary_button_width;
    }
    if (schema.has_secondary_button_height)
    {
        spec.height = schema.secondary_button_height;
    }
}

bool action_bar_first()
{
    const ChatComposeSchema schema = active_schema();
    return schema.has_action_bar_position && schema.action_bar_first;
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
    RootSpec resolved = spec;
    apply_root_schema(resolved);

    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, resolved.pad_row, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    apply_component_profile(root, ::ui::theme::ComponentSlot::ChatComposeRoot);
    make_non_scrollable(root);
    return root;
}

lv_obj_t* create_header_container(lv_obj_t* parent, const HeaderSpec& spec)
{
    lv_obj_t* header = lv_obj_create(parent);
    lv_obj_set_size(header, LV_PCT(100), spec.height > 0 ? spec.height : default_header_height());
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    make_non_scrollable(header);
    return header;
}

lv_obj_t* create_content_region(lv_obj_t* parent, const ContentSpec& spec)
{
    ContentSpec resolved = spec;
    apply_content_schema(resolved);

    lv_obj_t* content = lv_obj_create(parent);
    lv_obj_set_size(content, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(content, resolved.pad_all, 0);
    lv_obj_set_style_pad_row(content, resolved.pad_row, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    apply_component_profile(content, ::ui::theme::ComponentSlot::ChatComposeContentRegion);
    make_non_scrollable(content);
    return content;
}

lv_obj_t* create_editor(lv_obj_t* parent, const EditorSpec& spec)
{
    lv_obj_t* editor = lv_textarea_create(parent);
    lv_obj_set_width(editor, LV_PCT(100));
    lv_obj_set_flex_grow(editor, spec.grow ? 1 : 0);
    apply_component_profile(editor, ::ui::theme::ComponentSlot::ChatComposeEditor);
    return editor;
}

lv_obj_t* create_action_bar(lv_obj_t* parent, const ActionBarSpec& spec)
{
    ActionBarSpec resolved = spec;
    apply_action_bar_schema(resolved);

    lv_obj_t* bar = lv_obj_create(parent);
    if (resolved.height != LV_SIZE_CONTENT)
    {
        lv_obj_set_size(bar, LV_PCT(100), resolved.height);
    }
    else
    {
        lv_obj_set_width(bar, LV_PCT(100));
    }
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, resolved.main_align, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(bar, resolved.pad_left, 0);
    lv_obj_set_style_pad_right(bar, resolved.pad_right, 0);
    lv_obj_set_style_pad_top(bar, resolved.pad_top, 0);
    lv_obj_set_style_pad_bottom(bar, resolved.pad_bottom, 0);
    lv_obj_set_style_pad_row(bar, resolved.pad_row, 0);
    lv_obj_set_style_pad_column(bar, resolved.pad_column, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    apply_component_profile(bar, ::ui::theme::ComponentSlot::ChatComposeActionBar);
    make_non_scrollable(bar);
    if (action_bar_first())
    {
        lv_obj_move_to_index(bar, 1);
    }
    return bar;
}

lv_obj_t* create_action_button(lv_obj_t* parent,
                               lv_obj_t*& out_label,
                               ActionButtonRole role,
                               const ActionButtonSpec& spec)
{
    ActionButtonSpec resolved = spec;
    apply_button_schema(role, resolved);

    lv_obj_t* button = lv_btn_create(parent);
    lv_obj_set_size(button, resolved.width, resolved.height);
    apply_component_profile(button,
                            role == ActionButtonRole::Primary
                                ? ::ui::theme::ComponentSlot::ActionButtonPrimary
                                : ::ui::theme::ComponentSlot::ActionButtonSecondary);
    out_label = lv_label_create(button);
    apply_component_profile(out_label,
                            role == ActionButtonRole::Primary
                                ? ::ui::theme::ComponentSlot::ActionButtonPrimary
                                : ::ui::theme::ComponentSlot::ActionButtonSecondary);
    lv_obj_set_width(out_label, LV_PCT(100));
    lv_obj_set_style_text_align(out_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(out_label);
    make_non_scrollable(button);
    return button;
}

lv_obj_t* create_flex_spacer(lv_obj_t* parent)
{
    lv_obj_t* spacer = lv_obj_create(parent);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_radius(spacer, 0, 0);
    make_non_scrollable(spacer);
    return spacer;
}

} // namespace ui::presentation::chat_compose_scaffold

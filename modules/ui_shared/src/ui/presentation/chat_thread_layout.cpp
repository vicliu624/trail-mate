#include "ui/presentation/chat_thread_layout.h"

#include "ui/presentation/presentation_registry.h"
#include "ui/theme/theme_component_style.h"

namespace ui::presentation::chat_thread_layout
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

ChatConversationSchema active_schema()
{
    ChatConversationSchema schema{};
    (void)::ui::presentation::resolve_active_chat_conversation_schema(schema);
    return schema;
}

lv_flex_align_t to_lv_flex_align(SchemaAlign align)
{
    switch (align)
    {
    case SchemaAlign::Start:
        return LV_FLEX_ALIGN_START;
    case SchemaAlign::End:
        return LV_FLEX_ALIGN_END;
    case SchemaAlign::Center:
    default:
        return LV_FLEX_ALIGN_CENTER;
    }
}

void apply_root_schema(RootSpec& spec)
{
    const ChatConversationSchema schema = active_schema();
    if (schema.has_root_pad_row)
    {
        spec.pad_row = schema.root_pad_row;
    }
}

void apply_thread_schema(ThreadSpec& spec)
{
    const ChatConversationSchema schema = active_schema();
    if (schema.has_thread_pad_row)
    {
        spec.pad_row = schema.thread_pad_row;
    }
}

void apply_action_bar_schema(ActionBarSpec& spec)
{
    const ChatConversationSchema schema = active_schema();
    if (schema.has_action_bar_height)
    {
        spec.height = schema.action_bar_height;
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
    const ChatConversationSchema schema = active_schema();
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
    }
}

bool action_bar_first()
{
    const ChatConversationSchema schema = active_schema();
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
    apply_component_profile(root, ::ui::theme::ComponentSlot::ChatConversationRoot);
    make_non_scrollable(root);
    return root;
}

lv_obj_t* create_thread_region(lv_obj_t* parent, const ThreadSpec& spec)
{
    ThreadSpec resolved = spec;
    apply_thread_schema(resolved);

    lv_obj_t* thread = lv_obj_create(parent);
    lv_obj_set_width(thread, LV_PCT(100));
    lv_obj_set_flex_grow(thread, 1);
    lv_obj_set_flex_flow(thread, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(thread, resolved.pad_row, 0);
    lv_obj_set_style_bg_opa(thread, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(thread, 0, 0);
    lv_obj_set_style_radius(thread, 0, 0);
    apply_component_profile(thread, ::ui::theme::ComponentSlot::ChatConversationThreadRegion);
    lv_obj_set_scroll_dir(thread, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(thread, resolved.scrollbar_mode);
    return thread;
}

lv_obj_t* create_action_bar(lv_obj_t* parent, const ActionBarSpec& spec)
{
    ActionBarSpec resolved = spec;
    apply_action_bar_schema(resolved);

    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), resolved.height);
    lv_obj_set_flex_grow(bar, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar,
                          resolved.main_align,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, resolved.pad_column, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    apply_component_profile(bar, ::ui::theme::ComponentSlot::ChatConversationActionBar);
    make_non_scrollable(bar);
    if (action_bar_first())
    {
        lv_obj_move_to_index(bar, 0);
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
    make_non_scrollable(button);
    out_label = lv_label_create(button);
    apply_component_profile(out_label,
                            role == ActionButtonRole::Primary
                                ? ::ui::theme::ComponentSlot::ActionButtonPrimary
                                : ::ui::theme::ComponentSlot::ActionButtonSecondary);
    lv_obj_center(out_label);
    return button;
}

lv_obj_t* create_thread_row(lv_obj_t* parent)
{
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    make_non_scrollable(row);
    return row;
}

lv_obj_t* create_bubble(lv_obj_t* parent)
{
    lv_obj_t* bubble = lv_obj_create(parent);
    lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(bubble, LV_SIZE_CONTENT);
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(bubble, 0);
    make_non_scrollable(bubble);
    return bubble;
}

lv_obj_t* create_bubble_text(lv_obj_t* parent)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    return label;
}

lv_obj_t* create_bubble_time(lv_obj_t* parent)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    return label;
}

lv_obj_t* create_bubble_status(lv_obj_t* parent)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    return label;
}

void align_thread_row(lv_obj_t* row, bool is_self)
{
    if (!row)
    {
        return;
    }
    lv_obj_set_flex_align(row,
                          is_self ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
}

void set_bubble_max_width(lv_obj_t* bubble, lv_coord_t max_w)
{
    if (!bubble)
    {
        return;
    }
    lv_obj_set_style_max_width(bubble, max_w, LV_PART_MAIN);
}

lv_coord_t thread_content_width(lv_obj_t* thread_region)
{
    return thread_region ? lv_obj_get_content_width(thread_region) : 0;
}

} // namespace ui::presentation::chat_thread_layout

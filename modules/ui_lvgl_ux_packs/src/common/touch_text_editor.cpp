#include "ui_lvgl_ux_packs/common/touch_text_editor.h"

#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/widgets/ime/ime_widget.h"

#include <string>

namespace ui::widgets
{
namespace
{
lv_obj_t* s_root = nullptr;
lv_obj_t* s_source = nullptr;
lv_obj_t* s_draft = nullptr;
ImeWidget* s_owner = nullptr;
ImeWidget s_keyboard;

void finish_editor(bool apply)
{
    if (!s_root) return;
    lv_obj_t* source = s_source;
    ImeWidget* owner = s_owner;
    const std::string text = apply && s_draft ? lv_textarea_get_text(s_draft) : "";
    lv_obj_t* root = s_root;
    s_root = nullptr;
    s_source = nullptr;
    s_draft = nullptr;
    s_owner = nullptr;
    s_keyboard.detach();
    lv_obj_delete(root);
    if (source && lv_obj_is_valid(source))
    {
        if (owner) owner->activate();
        if (apply)
        {
            if (owner) owner->setText(text.c_str());
            else lv_textarea_set_text(source, text.c_str());
        }
    }
}

void on_action(lv_event_t* event)
{
    finish_editor(lv_event_get_user_data(event) != nullptr);
}

void on_root_deleted(lv_event_t*)
{
    if (!s_root) return;
    lv_obj_t* source = s_source;
    ImeWidget* owner = s_owner;
    s_root = nullptr;
    s_source = nullptr;
    s_draft = nullptr;
    s_owner = nullptr;
    s_keyboard.detach();
    if (source && owner && lv_obj_is_valid(source)) owner->activate();
}

void add_action(lv_obj_t* row, const char* text, bool apply)
{
    lv_obj_t* button = lv_button_create(row);
    lv_obj_set_size(button, 82, 28);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xE7C98F), 0);
    lv_obj_t* label = lv_label_create(button);
    ::ui::i18n::set_label_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0x3A2A1A), 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, on_action, LV_EVENT_CLICKED, apply ? button : nullptr);
}

void open_editor(lv_obj_t* source, ImeWidget* owner)
{
    if (s_root || !source || !lv_obj_is_valid(source)) return;
    s_source = source;
    s_owner = owner;
    s_root = lv_obj_create(lv_layer_top());
    lv_obj_add_event_cb(s_root, on_root_deleted, LV_EVENT_DELETE, nullptr);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 3, 0);
    lv_obj_set_style_pad_row(s_root, 3, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0xFFF7E9), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_t* actions = lv_obj_create(s_root);
    lv_obj_set_size(actions, LV_PCT(100), 28);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
    add_action(actions, "Cancel", false);
    add_action(actions, "OK", true);
    s_draft = lv_textarea_create(s_root);
    lv_obj_set_size(s_draft, LV_PCT(100), 40);
    lv_obj_set_style_pad_all(s_draft, 3, 0);
    lv_textarea_set_one_line(s_draft, lv_textarea_get_one_line(source));
    lv_textarea_set_password_mode(s_draft, lv_textarea_get_password_mode(source));
    lv_textarea_set_max_length(s_draft, lv_textarea_get_max_length(source));
    lv_textarea_set_accepted_chars(s_draft, lv_textarea_get_accepted_chars(source));
    lv_textarea_set_text(s_draft, lv_textarea_get_text(source));
    s_keyboard.init(s_root, s_draft, true);
    if (owner) s_keyboard.setMode(owner->mode());
}

void on_field_event(lv_event_t* event)
{
    auto* field = static_cast<lv_obj_t*>(lv_event_get_target(event));
    if (lv_event_get_code(event) == LV_EVENT_DELETE)
    {
        if (s_source == field)
        {
            s_source = nullptr;
            s_owner = nullptr;
            finish_editor(false);
        }
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
    {
        open_editor(field, static_cast<ImeWidget*>(lv_event_get_user_data(event)));
    }
}
} // namespace

void attach_touch_text_editor(lv_obj_t* textarea, ImeWidget* owner)
{
    if (!textarea || !::ui::page_profile::current().compact_touch_keyboard) return;
    if (s_source == textarea && s_owner != owner) finish_editor(false);
    lv_obj_remove_event_cb(textarea, on_field_event);
    lv_obj_add_event_cb(textarea, on_field_event, LV_EVENT_ALL, owner);
}

TouchEditorCapture capture_touch_text_editor(lv_obj_t* source, char* text,
                                             std::size_t capacity, ImeEditState& state)
{
    if (!source || s_source != source || !s_root || !s_draft) return TouchEditorCapture::Inactive;
    return s_keyboard.captureEditState(text, capacity, state) ? TouchEditorCapture::Captured
                                                              : TouchEditorCapture::InsufficientCapacity;
}

bool restore_touch_text_editor(lv_obj_t* source, const char* text,
                               const ImeEditState& state, ImeWidget* owner)
{
    if (!text) return false;
    return restore_touch_text_editor(
        source, state, [](const void* value, lv_obj_t* draft)
        {
                                         lv_textarea_set_text(draft, static_cast<const char*>(value));
                                         return true; },
        text, owner);
}

TouchEditorCapture capture_touch_text_editor(lv_obj_t* source, TouchTextCapture capture,
                                             void* context, ImeEditState& state)
{
    if (!source || s_source != source || !s_root || !s_draft) return TouchEditorCapture::Inactive;
    return capture && s_keyboard.captureEditState(nullptr, 0, state) && capture(context, lv_textarea_get_text(s_draft))
               ? TouchEditorCapture::Captured
               : TouchEditorCapture::InsufficientCapacity;
}

bool restore_touch_text_editor(lv_obj_t* source, const ImeEditState& state,
                               TouchTextRestore restore, const void* context, ImeWidget* owner)
{
    if (!restore || s_root || !source || !lv_obj_is_valid(source) ||
        !::ui::page_profile::current().compact_touch_keyboard) return false;
    open_editor(source, owner);
    if (s_source == source && s_draft && restore(context, s_draft) &&
        s_keyboard.restoreEditState(lv_textarea_get_text(s_draft), state)) return true;
    finish_editor(false);
    return false;
}
} // namespace ui::widgets

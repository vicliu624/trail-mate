#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/screens/agenda/agenda_editor_runtime.h"
#include "ui/screens/agenda/agenda_page_components.h"
#include "ui/screens/agenda/agenda_page_input.h"
#include "ui/screens/agenda/agenda_page_runtime.h"
#include "ui/ui_theme.h"
#include "ui_lvgl_ux_packs/common/touch_text_editor.h"
#include "ui_presentation/waypoint/waypoint_model.h"

#include <cstdio>
#include <cstring>

namespace ui::agenda::page::editor
{
namespace
{
constexpr const char* kReminderLabels[] = {"None", "At time", "10 min before", "30 min before", "1 hour before", "1 day before"};
constexpr const char* kRepeatLabels[] = {"None", "Daily", "Weekly", "Monthly", "Yearly"};

void box(lv_obj_t* object)
{
    lv_obj_set_style_bg_color(object, ::ui::theme::page_bg(), 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
}
void put(lv_obj_t* object, int x, int y, int width, int height = LV_SIZE_CONTENT)
{
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
}
void selected(lv_obj_t* object, EditorField field)
{
    if (state()->draft.focus == field) state()->editor_widgets.focus = object;
}
void remember_field(lv_event_t* event)
{
    if (auto* s = state()) s->draft.focus = static_cast<EditorField>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
}
lv_obj_t* text_field(lv_obj_t* parent, const char* text, int bytes, EditorField field)
{
    auto* object = lv_textarea_create(parent);
    lv_textarea_set_one_line(object, true);
    lv_textarea_set_max_length(object, bytes - 1);
    lv_textarea_set_text(object, text);
    lv_obj_set_style_pad_all(object, 3, 0);
    lv_obj_set_style_bg_color(object, ::ui::theme::surface(), 0);
    lv_obj_set_style_border_color(object, ::ui::theme::border(), 0);
    ::ui::fonts::apply_content_font(object, text, ::ui::page_profile::resolve_body_font());
    input::bind(object);
    ::ui::widgets::attach_touch_text_editor(object);
    lv_obj_add_event_cb(object, remember_field, LV_EVENT_FOCUSED, reinterpret_cast<void*>(static_cast<uintptr_t>(field)));
    selected(object, field);
    return object;
}
void field(lv_obj_t* parent, int width, int y, const char* name, const char* value,
           Action action, EditorField focus, bool raw = false)
{
    auto* caption = components::addLabel(parent, name, true, true);
    put(caption, 3, y + 6, 82);
    auto* button = components::addButton(parent, raw ? nullptr : value, action);
    put(button, 88, y, width - 94, 28);
    if (raw)
    {
        auto* label = components::addLabel(button, value, false);
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_center(label);
    }
    if (action == Action::None) lv_obj_add_state(button, LV_STATE_DISABLED);
    lv_obj_add_event_cb(button, remember_field, LV_EVENT_FOCUSED, reinterpret_cast<void*>(static_cast<uintptr_t>(focus)));
    selected(button, focus);
}

void form(int width, int height)
{
    auto& s = *state();
    if (s.confirm_discard)
    {
        put(components::addLabel(s.body, "Discard changes?"), 4, 8, width - 8);
        put(components::addButton(s.body, "Keep editing", Action::KeepEditing), 4, 48, width - 8, 32);
        put(components::addButton(s.body, "Discard", Action::Discard), 4, 88, width - 8, 32);
        return;
    }
    auto* form = lv_obj_create(s.body);
    box(form);
    put(form, 0, 0, width, height - 34);
    lv_obj_set_scroll_dir(form, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(form, LV_SCROLLBAR_MODE_AUTO);
    put(components::addLabel(form, "Title", true, true), 3, 0, width - 6);
    s.editor_widgets.title = text_field(form, s.draft.event.title, sizeof(s.draft.event.title), EditorField::Title);
    put(s.editor_widgets.title, 3, 20, width - 9, 30);
    ::agenda::CivilTime date;
    ::agenda::fromCalendarSeconds(s.draft.event.start_time, date);
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u", date.year, date.month, date.day);
    field(form, width, 54, "Date", buffer, Action::PickDate, EditorField::Date, true);
    std::snprintf(buffer, sizeof(buffer), "%02u:%02u", date.hour, date.minute);
    field(form, width, 86, "Time", buffer, Action::PickTime, EditorField::Time, true);
    field(form, width, 118, "Reminder", reminderText(s.draft.event), Action::PickReminder, EditorField::Reminder);
    const bool location = (s.draft.event.flags & ::agenda::HasLocation) != 0;
    field(form, width, 150, "Location", location ? s.draft.event.location_name : "Not set",
          Action::PickLocation, EditorField::Location, location);
    field(form, width, 182, "Repeat", repeatText(s.draft.event.repeat), Action::PickRepeat, EditorField::Repeat);
    put(components::addLabel(form, "Note", true, true), 3, 216, width - 6);
    s.editor_widgets.note = text_field(form, s.draft.event.note, sizeof(s.draft.event.note), EditorField::Note);
    put(s.editor_widgets.note, 3, 236, width - 9, 30);
    if (s.draft.event.repeat != ::agenda::Repeat::None)
        put(components::addLabel(form, "Changes apply to the entire series", true, true), 3, 270, width - 9);
    auto* save = components::addButton(s.body, "Save", Action::Save);
    put(save, 4, height - 30, width - 8, 28);
    if (s.awaiting_command)
    {
        // Disabled state is not inherited by child widgets. Freeze the actual
        // editors while the captured command is pending, not just their panel.
        lv_obj_add_state(s.editor_widgets.title, LV_STATE_DISABLED);
        lv_obj_add_state(s.editor_widgets.note, LV_STATE_DISABLED);
    }
}

void calendar_draw(lv_event_t* event)
{
    // Calendar's own draw callback overrides styles with the global theme's
    // primary colour. Adapt only this calendar, after its built-in callback.
    auto* task = static_cast<lv_draw_task_t*>(lv_event_get_param(event));
    auto* fill = lv_draw_task_get_fill_dsc(task);
    auto* border = lv_draw_task_get_border_dsc(task);
    if (fill && fill->base.part == LV_PART_ITEMS) fill->color = ::ui::theme::accent();
    if (border && border->base.part == LV_PART_ITEMS) border->color = ::ui::theme::border();
}

void date_chosen(lv_event_t* event)
{
    lv_calendar_date_t selected_date;
    // VALUE_CHANGED bubbles from Calendar's internal buttonmatrix. The
    // original target is not a calendar and cannot be passed to its API.
    auto* calendar = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    if (lv_calendar_get_pressed_date(calendar, &selected_date) != LV_RESULT_OK) return;
    auto& date = state()->picker.date;
    date.year = selected_date.year;
    date.month = selected_date.month;
    date.day = selected_date.day;
    runtime::request(Action::SelectDay, date.day);
}
void date_picker(int width, int height)
{
    auto& s = *state();
    auto& picker = s.picker;
    put(components::addButton(s.body, LV_SYMBOL_LEFT, Action::PreviousMonth), 3, 0, 40, 26);
    put(components::addButton(s.body, LV_SYMBOL_RIGHT, Action::NextMonth), width - 43, 0, 40, 26);
    char month[20];
    std::snprintf(month, sizeof(month), "%04u-%02u", picker.date.year, picker.date.month);
    auto* caption = components::addLabel(s.body, month, false);
    put(caption, 46, 3, width - 92);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    auto* calendar = lv_calendar_create(s.body);
    put(calendar, 3, 29, width - 6, height - 29);
    lv_obj_set_style_bg_color(calendar, ::ui::theme::surface(), 0);
    lv_obj_set_style_border_color(calendar, ::ui::theme::border(), 0);
    lv_obj_set_style_text_font(calendar, ::ui::page_profile::resolve_body_font(), 0);
    lv_obj_set_style_pad_all(calendar, 1, 0);
    auto* days_grid = lv_calendar_get_btnmatrix(calendar);
    lv_obj_set_style_bg_color(days_grid, ::ui::theme::surface(), 0);
    lv_obj_set_style_text_color(days_grid, ::ui::theme::text(), LV_PART_ITEMS);
    lv_obj_set_style_text_color(days_grid, ::ui::theme::text(), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(days_grid, ::ui::theme::text(), LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(days_grid, ::ui::theme::accent(), LV_STATE_FOCUS_KEY);
    lv_obj_add_event_cb(days_grid, calendar_draw, LV_EVENT_DRAW_TASK_ADDED, nullptr);
    lv_calendar_set_month_shown(calendar, picker.date.year, picker.date.month);
    ::agenda::CivilTime today;
    ::agenda::fromCalendarSeconds(s.snapshot.today_start, today);
    lv_calendar_set_today_date(calendar, today.year, today.month, today.day);
    const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    for (unsigned i = 0; i < 7; ++i) picker.day_names[i] = ::ui::i18n::tr(days[i]);
    lv_calendar_set_day_names(calendar, picker.day_names);
    s.host->source->month(picker.date.year, picker.date.month, picker.month);
    unsigned count = 0;
    for (unsigned day = 1; day <= 31; ++day)
        if (picker.month.occupied_days & (uint32_t{1} << (day - 1)))
            picker.markers[count++] = {picker.date.year, picker.date.month, static_cast<uint8_t>(day)};
    lv_calendar_set_highlighted_dates(calendar, picker.markers, count);
    lv_obj_add_event_cb(calendar, date_chosen, LV_EVENT_VALUE_CHANGED, nullptr);
    // Calendar is a container; its buttonmatrix owns keyboard/encoder editing.
    // Do not leave the non-editable container as a second focus stop.
    lv_group_remove_obj(calendar);
    input::bind(days_grid);
    char selected_day[4];
    std::snprintf(selected_day, sizeof(selected_day), "%u", picker.date.day);
    for (unsigned id = 7; id < 49; ++id)
    {
        if (!lv_buttonmatrix_has_button_ctrl(days_grid, id, LV_BUTTONMATRIX_CTRL_DISABLED) &&
            std::strcmp(lv_buttonmatrix_get_button_text(days_grid, id), selected_day) == 0)
        {
            lv_buttonmatrix_set_selected_button(days_grid, id);
            break;
        }
    }
    s.editor_widgets.focus = days_grid;
    if (picker.month.result != ::agenda::StoreResult::Ok) s.error = "Could not read events";
}

void step_time(lv_event_t* event)
{
    const auto data = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    auto* field = state()->editor_widgets.clock_fields[data >> 1];
    if (data & 1) lv_spinbox_increment(field);
    else lv_spinbox_decrement(field);
}
void clock_field(int index, int x, int y, int value, int maximum)
{
    auto& s = *state();
    auto* spin = lv_spinbox_create(s.body);
    lv_spinbox_set_range(spin, 0, maximum);
    lv_spinbox_set_digit_format(spin, 2, 0);
    lv_spinbox_set_rollover(spin, true);
    lv_spinbox_set_step(spin, 1);
    lv_spinbox_set_value(spin, value);
    put(spin, x + 26, y, 46, 30);
    lv_obj_set_style_pad_all(spin, 4, 0);
    lv_obj_set_style_bg_color(spin, ::ui::theme::surface(), 0);
    lv_obj_set_style_border_color(spin, ::ui::theme::border(), 0);
    lv_obj_set_style_text_color(spin, ::ui::theme::text(), 0);
    lv_obj_set_style_text_font(spin, ::ui::page_profile::resolve_body_font(), 0);
    lv_obj_set_style_outline_color(spin, ::ui::theme::accent(), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_color(spin, ::ui::theme::accent(), LV_PART_CURSOR);
    lv_obj_set_style_text_color(spin, ::ui::theme::text(), LV_PART_CURSOR);
    s.editor_widgets.clock_fields[index] = spin;
    input::bind(spin);
    for (unsigned increment = 0; increment < 2; ++increment)
    {
        auto* button = components::addButton(s.body, increment ? "+" : "-", Action::None);
        lv_obj_remove_event_cb(button, input::activate);
        put(button, x + (increment ? 74 : 0), y, 24, 30);
        lv_obj_add_event_cb(button, step_time, LV_EVENT_CLICKED,
                            reinterpret_cast<void*>((static_cast<uintptr_t>(index) << 1) | increment));
    }
}
void time_picker(int width, int height)
{
    auto& s = *state();
    ::agenda::CivilTime start, end;
    ::agenda::fromCalendarSeconds(s.draft.event.start_time, start);
    ::agenda::fromCalendarSeconds(s.draft.event.flags & ::agenda::HasEndTime ? s.draft.event.end_time : s.draft.event.start_time, end);
    put(components::addLabel(s.body, "Start time", true, true), 4, 0, width - 8);
    clock_field(0, width / 2 - 110, 21, start.hour, 23);
    clock_field(1, width / 2 + 10, 21, start.minute, 59);
    s.editor_widgets.end_enabled = lv_checkbox_create(s.body);
    lv_checkbox_set_text(s.editor_widgets.end_enabled, ::ui::i18n::tr("End time (optional)"));
    put(s.editor_widgets.end_enabled, 4, 60, width - 8, 24);
    lv_obj_set_style_text_color(s.editor_widgets.end_enabled, ::ui::theme::text(), 0);
    lv_obj_set_style_text_font(s.editor_widgets.end_enabled, ::ui::page_profile::resolve_body_font(), 0);
    lv_obj_set_style_bg_color(s.editor_widgets.end_enabled, ::ui::theme::surface(), LV_PART_INDICATOR);
    lv_obj_set_style_border_color(s.editor_widgets.end_enabled, ::ui::theme::border(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s.editor_widgets.end_enabled, ::ui::theme::accent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(s.editor_widgets.end_enabled, ::ui::theme::border(), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(s.editor_widgets.end_enabled, ::ui::theme::text(), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_anim_duration(s.editor_widgets.end_enabled, 0, LV_PART_INDICATOR);
    if (s.draft.event.flags & ::agenda::HasEndTime) lv_obj_add_state(s.editor_widgets.end_enabled, LV_STATE_CHECKED);
    input::bind(s.editor_widgets.end_enabled);
    clock_field(2, width / 2 - 110, 90, end.hour, 23);
    clock_field(3, width / 2 + 10, 90, end.minute, 59);
    // Keep the helper above the footer even on the 222-pixel-tall profile.
    put(components::addLabel(s.body, "End <= start: next day", true, true), 4, 121, width - 8, 14);
    put(components::addButton(s.body, "OK", Action::AcceptTime), 4, height - 28, width - 8, 28);
    s.editor_widgets.focus = s.editor_widgets.clock_fields[0];
}
void presets(int width, int height, bool reminder)
{
    auto& s = *state();
    const unsigned count = reminder ? 6 : 5;
    const auto* labels = reminder ? kReminderLabels : kRepeatLabels;
    const int row_height = height / count;
    for (unsigned i = 0; i < count; ++i)
    {
        auto* button = components::addButton(s.body, labels[i], reminder ? Action::SelectReminder : Action::SelectRepeat, i);
        put(button, 4, i * row_height, width - 8, row_height - 2);
        if (i == 0) s.editor_widgets.focus = button;
    }
}
void location_picker(int width, int height)
{
    auto& s = *state();
    put(components::addLabel(s.body, "Location is optional", true, true), 4, 0, width - 8);
    auto* current = components::addButton(s.body, "Current position", Action::CurrentLocation);
    put(current, 4, 24, width - 8, 32);
    if (!s.host->locations) lv_obj_add_state(current, LV_STATE_DISABLED);
    s.editor_widgets.focus = current;
    auto* map = components::addButton(s.body, "Choose on map", Action::ChooseOnMap);
    put(map, 4, 60, width - 8, 32);
    if (!s.host->request_map) lv_obj_add_state(map, LV_STATE_DISABLED);
    auto* saved = components::addButton(s.body, "Saved waypoint", Action::PickWaypoint);
    put(saved, 4, 96, width - 8, 28);
    if (!s.host->waypoints) lv_obj_add_state(saved, LV_STATE_DISABLED);
    auto* remove = components::addButton(s.body, "Remove location", Action::ClearLocation);
    put(remove, 4, height - 32, width - 8, 30);
    if (!(s.draft.event.flags & ::agenda::HasLocation)) lv_obj_add_state(remove, LV_STATE_DISABLED);
}
void waypoint_picker(int width, int height)
{
    auto& s = *state();
    ::waypoint::Page page;
    const auto capacity = static_cast<uint8_t>((height - 32) / 24 < 5 ? (height - 32) / 24 : 5);
    s.waypoint_page.count = 0;
    s.waypoint_page.has_more = false;
    if (!s.host->waypoints || s.host->waypoints->page(s.waypoint_page.after, capacity, page) != ::waypoint::Result::Ok)
        s.error = "Could not read waypoints";
    else
    {
        s.waypoint_page.count = page.count;
        s.waypoint_page.has_more = page.has_more;
        for (uint8_t i = 0; i < page.count; ++i)
        {
            s.waypoint_page.ids[i] = page.rows[i].id;
            auto* button = components::addButton(s.body, nullptr, Action::SelectWaypoint, i);
            put(button, 4, i * 24, width - 8, 23);
            auto* label = components::addLabel(button, page.rows[i].name, false);
            lv_obj_set_width(label, LV_PCT(100));
            lv_obj_center(label);
            if (!i) s.editor_widgets.focus = button;
        }
        if (!page.count) put(components::addLabel(s.body, "No saved waypoints"), 4, 4, width - 8);
    }
    auto* first = components::addButton(s.body, "First", Action::FirstWaypoints);
    put(first, 4, height - 28, width / 3 - 6, 28);
    if (!s.waypoint_page.after) lv_obj_add_state(first, LV_STATE_DISABLED);
    auto* next = components::addButton(s.body, "More", Action::NextWaypoints);
    put(next, width / 3 + 2, height - 28, width / 3 - 6, 28);
    if (!page.has_more) lv_obj_add_state(next, LV_STATE_DISABLED);
    auto* save = components::addButton(s.body, "New", Action::NameWaypoint);
    put(save, 2 * width / 3, height - 28, width / 3 - 4, 28);
    if (!(s.draft.event.flags & ::agenda::HasLocation) || !s.host->waypoint_actions) lv_obj_add_state(save, LV_STATE_DISABLED);
}
} // namespace

const char* reminderText(const ::agenda::EventRecord& event)
{
    if (!(event.flags & ::agenda::HasReminder)) return kReminderLabels[0];
    switch (event.reminder_offset_sec)
    {
    case 600:
        return kReminderLabels[2];
    case 1800:
        return kReminderLabels[3];
    case 3600:
        return kReminderLabels[4];
    case 86400:
        return kReminderLabels[5];
    default:
        return kReminderLabels[1];
    }
}
const char* repeatText(::agenda::Repeat repeat)
{
    const auto index = static_cast<unsigned>(repeat);
    return kRepeatLabels[index < sizeof(kRepeatLabels) / sizeof(kRepeatLabels[0]) ? index : 0];
}
const char* title()
{
    switch (state()->view)
    {
    case View::DatePicker:
        return "Pick Date";
    case View::TimePicker:
        return "Pick Time";
    case View::ReminderPicker:
        return "Reminder";
    case View::RepeatPicker:
        return "Repeat";
    case View::LocationPicker:
        return "Location";
    case View::WaypointPicker:
        return "Saved waypoint";
    case View::WaypointName:
        return "Saved waypoint";
    default:
        return state()->draft.editing_existing ? "Edit Event" : "New Event";
    }
}
void showStatus()
{
    auto& s = *state();
    if (!s.editor_widgets.status) return;
    const char* key = s.awaiting_command ? "Saving..." : s.error ? s.error
                                                                 : "";
    ::ui::i18n::set_label_text(s.editor_widgets.status, key);
}
void updateDiscard()
{
    auto& s = *state();
    lv_group_remove_all_objs(s.group);
    input::bind(s.top_bar.back_btn);
    if (s.confirm_discard)
    {
        if (!s.editor_widgets.confirm_root)
        {
            auto* panel = lv_obj_create(s.body);
            box(panel);
            lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
            lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
            lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
            s.editor_widgets.confirm_root = panel;
            const int width = lv_obj_get_width(s.body);
            put(components::addLabel(panel, "Discard changes?"), 4, 8, width - 8);
            auto* keep = components::addButton(panel, "Keep editing", Action::KeepEditing);
            put(keep, 4, 48, width - 8, 32);
            put(components::addButton(panel, "Discard", Action::Discard), 4, 88, width - 8, 32);
            lv_group_focus_obj(keep);
        }
    }
    else
    {
        if (s.editor_widgets.confirm_root) lv_obj_delete(s.editor_widgets.confirm_root);
        s.editor_widgets.confirm_root = nullptr;
        const auto bind_tree = [](auto&& self, lv_obj_t* object) -> void
        {
            if (lv_obj_check_type(object, &lv_button_class) || lv_obj_check_type(object, &lv_textarea_class)) input::bind(object);
            for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i) self(self, lv_obj_get_child(object, i));
        };
        bind_tree(bind_tree, s.body);
        lv_group_focus_obj(s.editor_widgets.title);
        showStatus();
    }
    lv_group_set_editing(s.group, false);
}
void render(lv_coord_t width, lv_coord_t height)
{
    auto& s = *state();
    s.editor_widgets.status = components::addLabel(s.body, "", true, true);
    put(s.editor_widgets.status, 4, height - 18, width - 8, 18);
    height -= 20;
    switch (s.view)
    {
    case View::Editor:
        form(width, height);
        break;
    case View::DatePicker:
        date_picker(width, height);
        break;
    case View::TimePicker:
        time_picker(width, height);
        break;
    case View::ReminderPicker:
        presets(width, height, true);
        break;
    case View::RepeatPicker:
        presets(width, height, false);
        break;
    case View::LocationPicker:
        location_picker(width, height);
        break;
    case View::WaypointPicker:
        waypoint_picker(width, height);
        break;
    case View::WaypointName:
        put(components::addLabel(s.body, "Name"), 4, 4, width - 8);
        s.editor_widgets.title = text_field(s.body, s.draft.event.location_name, 32, EditorField::Location);
        put(s.editor_widgets.title, 4, 28, width - 8, 32);
        s.editor_widgets.focus = s.editor_widgets.title;
        put(components::addButton(s.body, "Save", Action::SaveWaypoint), 4, height - 28, width - 8, 28);
        break;
    default:
        break;
    }
    showStatus();
}
} // namespace ui::agenda::page::editor

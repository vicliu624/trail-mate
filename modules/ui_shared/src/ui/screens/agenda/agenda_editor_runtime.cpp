#include "ui/screens/agenda/agenda_editor_runtime.h"
#include "ui_presentation/agenda/agenda_waypoint.h"
#include "ui_presentation/waypoint/waypoint_model.h"

#include <cstring>

namespace ui::agenda::page::editor
{
namespace
{
bool is_picker(View view)
{
    return view == View::DatePicker || view == View::TimePicker ||
           view == View::ReminderPicker || view == View::RepeatPicker || view == View::LocationPicker || view == View::WaypointPicker || view == View::WaypointName;
}

void open_date(bool jump)
{
    auto& s = *state();
    s.picker = {};
    s.picker.jump = jump;
    ::agenda::fromCalendarSeconds(jump ? s.request.day_start : s.draft.event.start_time, s.picker.date);
    s.view = View::DatePicker;
    s.draft.focus = EditorField::Date;
}

void discard()
{
    auto& s = *state();
    if (s.editor_return == View::Detail)
    {
        if (s.host->source->detail(s.draft.event.id, s.draft.event) != ::agenda::AgendaResult::Ok)
        {
            s.editor_return = View::Agenda;
            s.error = "Event is unavailable";
        }
    }
    else s.draft = {};
    s.draft.dirty = false;
    s.confirm_discard = false;
    s.view = s.editor_return;
}

bool change_start(int64_t start)
{
    auto& s = *state();
    auto& event = s.draft.event;
    const int64_t duration = event.flags & ::agenda::HasEndTime ? event.end_time - event.start_time : 0;
    if (start < 0 || start > ::agenda::kLastCalendarSecond - duration)
    {
        s.error = "Date or time is out of range";
        return false;
    }
    if (event.start_time != start) s.draft.dirty = true;
    event.start_time = start;
    if (duration) event.end_time = start + duration;
    return true;
}

bool accept_time()
{
    auto& s = *state();
    auto& event = s.draft.event;
    const int64_t day = event.start_time / 86400 * 86400;
    const int64_t start = day + lv_spinbox_get_value(s.editor_widgets.clock_fields[0]) * 3600 +
                          lv_spinbox_get_value(s.editor_widgets.clock_fields[1]) * 60;
    const bool has_end = lv_obj_has_state(s.editor_widgets.end_enabled, LV_STATE_CHECKED);
    int64_t end = day + lv_spinbox_get_value(s.editor_widgets.clock_fields[2]) * 3600 +
                  lv_spinbox_get_value(s.editor_widgets.clock_fields[3]) * 60;
    if (has_end && end <= start) end += 86400;
    if (has_end && end > ::agenda::kLastCalendarSecond)
    {
        s.error = "Date or time is out of range";
        return false;
    }
    s.draft.dirty |= start != event.start_time || (has_end != ((event.flags & ::agenda::HasEndTime) != 0)) ||
                     (has_end && end != event.end_time);
    event.start_time = start;
    event.end_time = has_end ? end : 0;
    if (has_end) event.flags |= ::agenda::HasEndTime;
    else event.flags &= ~::agenda::HasEndTime;
    return true;
}
} // namespace

bool captureText()
{
    auto& s = *state();
    if (s.view != View::Editor || !s.editor_widgets.title || !s.editor_widgets.note) return true;
    const char* title = lv_textarea_get_text(s.editor_widgets.title);
    const char* note = lv_textarea_get_text(s.editor_widgets.note);
    if (std::strlen(title) >= sizeof(s.draft.event.title) || std::strlen(note) >= sizeof(s.draft.event.note))
    {
        s.error = "Text exceeds the UTF-8 byte limit";
        return false; // Preserve the live fields; never silently truncate UTF-8.
    }
    s.draft.dirty |= std::strcmp(title, s.draft.event.title) != 0 || std::strcmp(note, s.draft.event.note) != 0;
    std::strcpy(s.draft.event.title, title);
    std::strcpy(s.draft.event.note, note);
    if (note[0]) s.draft.event.flags |= ::agenda::HasNote;
    else s.draft.event.flags &= ~::agenda::HasNote;
    return true;
}

Transition handle(Action action, uint8_t value)
{
    auto& s = *state();
    if (action == Action::NewEvent)
    {
        if (!s.host->source->newDraft(s.draft)) s.error = "Set the device time first";
        else
        {
            s.editor_return = View::Agenda;
            s.view = View::Editor;
            s.confirm_discard = false;
        }
        return Transition::Render;
    }
    if (action == Action::EditEvent)
    {
        s.draft.editing_existing = true;
        s.draft.dirty = false;
        s.draft.focus = EditorField::Title;
        s.editor_return = View::Detail;
        s.view = View::Editor;
        return Transition::Render;
    }
    if (action == Action::JumpToDate)
    {
        open_date(true);
        return Transition::Render;
    }
    if (action == Action::Back && is_picker(s.view))
    {
        s.view = s.view == View::WaypointName ? View::WaypointPicker : s.view == View::WaypointPicker            ? View::LocationPicker
                                                                   : s.view == View::DatePicker && s.picker.jump ? View::Agenda
                                                                                                                 : View::Editor;
        return Transition::Render;
    }
    if (action == Action::Discard)
    {
        discard();
        return Transition::Render;
    }
    if (action == Action::KeepEditing || (action == Action::Back && s.confirm_discard))
    {
        s.confirm_discard = false;
        return Transition::Render;
    }
    if (s.view == View::Editor)
    {
        const bool captured = captureText();
        if (action == Action::Back)
        {
            if (s.draft.dirty || !captured) s.confirm_discard = true;
            else discard();
            return Transition::Render;
        }
        if (!captured) return Transition::KeepWidgets;
    }
    switch (action)
    {
    case Action::NameWaypoint:
        if (!(s.draft.event.flags & ::agenda::HasLocation) || !s.host->waypoint_actions) return Transition::KeepWidgets;
        s.view = View::WaypointName;
        break;
    case Action::SaveWaypoint:
    {
        if (s.view != View::WaypointName || !s.host->waypoint_actions || !s.host->waypoints) return Transition::KeepWidgets;
        const char* name = lv_textarea_get_text(s.editor_widgets.title);
        ::waypoint::Record record;
        if (std::strlen(name) >= sizeof(record.name))
        {
            s.error = "Text exceeds the UTF-8 byte limit";
            return Transition::KeepWidgets;
        }
        std::strcpy(record.name, name);
        record.latitude_e7 = s.draft.event.latitude_e7;
        record.longitude_e7 = s.draft.event.longitude_e7;
        if (!s.host->waypoint_actions->save(record).ok)
        {
            s.error = "Could not save changes";
            return Transition::KeepWidgets;
        }
        s.pending_sequence = s.host->waypoints->commandResult().sequence;
        s.awaiting_command = true;
        return Transition::KeepWidgets;
    }
    case Action::PickWaypoint:
        s.waypoint_page = {};
        s.view = View::WaypointPicker;
        break;
    case Action::FirstWaypoints:
        s.waypoint_page.after = 0;
        break;
    case Action::NextWaypoints:
        if (s.waypoint_page.has_more && s.waypoint_page.count)
            s.waypoint_page.after = s.waypoint_page.ids[s.waypoint_page.count - 1];
        break;
    case Action::SelectWaypoint:
    {
        if (s.view != View::WaypointPicker || value >= s.waypoint_page.count) return Transition::KeepWidgets;
        ::waypoint::Record selected;
        if (!s.host->waypoints || s.host->waypoints->detail(s.waypoint_page.ids[value], selected) != ::waypoint::Result::Ok ||
            !applyWaypoint(s.draft, selected))
        {
            s.error = "Waypoint unavailable";
            return Transition::KeepWidgets;
        }
        s.view = View::Editor;
        break;
    }
    case Action::Save:
    {
        const auto result = s.host->actions->save(s.draft.event);
        if (!result.ok)
        {
            s.error = result.failure == ::ui::UiActionFailure::InvalidInput ? "Enter a title and valid date/time" : "Could not save changes";
            return Transition::KeepWidgets;
        }
        s.pending_sequence = s.host->source->commandResult().sequence;
        s.awaiting_command = true;
        return Transition::Render;
    }
    case Action::PickDate:
        open_date(false);
        break;
    case Action::PreviousMonth:
    case Action::NextMonth:
    {
        auto& date = s.picker.date;
        int month_index = date.year * 12 + date.month - 1 + (action == Action::NextMonth ? 1 : -1);
        if (month_index >= 1970 * 12 && month_index < 10000 * 12)
        {
            date.year = month_index / 12;
            date.month = month_index % 12 + 1;
            date.day = 1;
        }
        break;
    }
    case Action::SelectDay:
    {
        auto date = s.picker.date;
        date.day = value;
        int64_t start = 0;
        if (s.picker.jump) date.hour = date.minute = date.second = 0;
        if (!::agenda::toCalendarSeconds(date, start)) break;
        if (s.picker.jump)
        {
            s.request.day_start = start;
            s.request.after = {};
            s.host->source->snapshot(s.request, s.snapshot);
            s.view = View::Agenda;
        }
        else if (change_start(start)) s.view = View::Editor;
        break;
    }
    case Action::PickTime:
        s.draft.focus = EditorField::Time;
        s.view = View::TimePicker;
        break;
    case Action::AcceptTime:
        if (!accept_time()) return Transition::KeepWidgets;
        s.view = View::Editor;
        break;
    case Action::PickReminder:
        s.draft.focus = EditorField::Reminder;
        s.view = View::ReminderPicker;
        break;
    case Action::SelectReminder:
    {
        static constexpr uint32_t offsets[] = {0, 0, 600, 1800, 3600, 86400};
        if (value >= 6) break;
        s.draft.event.reminder_offset_sec = offsets[value];
        if (value) s.draft.event.flags |= ::agenda::HasReminder;
        else s.draft.event.flags &= ~::agenda::HasReminder;
        s.draft.dirty = true;
        s.view = View::Editor;
        break;
    }
    case Action::PickRepeat:
        s.draft.focus = EditorField::Repeat;
        s.view = View::RepeatPicker;
        break;
    case Action::PickLocation:
        s.draft.focus = EditorField::Location;
        s.view = View::LocationPicker;
        break;
    case Action::CurrentLocation:
    {
        AgendaCoordinate coordinate;
        if (!s.host->locations || !s.host->locations->currentLocation(coordinate) || !applyCoordinate(s.draft, coordinate))
        {
            s.error = "Current position unavailable";
            return Transition::KeepWidgets;
        }
        s.view = View::Editor;
        break;
    }
    case Action::ChooseOnMap:
        if (!s.host->request_map ||
            !s.host->request_map(s.host->map_context, s.draft, s.editor_return == View::Detail))
            s.error = "Map is unavailable on this target.";
        return Transition::KeepWidgets;
    case Action::ClearLocation:
        clearLocation(s.draft);
        s.view = View::Editor;
        break;
    case Action::SelectRepeat:
        if (value > static_cast<uint8_t>(::agenda::Repeat::Yearly)) break;
        s.draft.event.repeat = static_cast<::agenda::Repeat>(value);
        s.draft.dirty = true;
        s.view = View::Editor;
        break;
    default:
        return Transition::Unhandled;
    }
    return Transition::Render;
}
} // namespace ui::agenda::page::editor

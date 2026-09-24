#include "ui/screens/agenda/agenda_page_flow.h"

#include "ui/app_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/page/page_profile.h"
#include "ui/screens/agenda/agenda_editor_runtime.h"
#include "ui/screens/agenda/agenda_page_components.h"
#include "ui/screens/agenda/agenda_page_runtime.h"
#include "ui/screens/agenda/agenda_text_snapshot.h"
#include "ui/screens/gps/gps_page_runtime.h"
#include "ui/widgets/ime/ime_widget.h"
#include "ui_lvgl_ux_packs/common/touch_text_editor.h"
#include "ui_presentation/map/map_location_request.h"
#include "ui_presentation/map/map_target_request.h"

#include <cstring>
#include <new>

namespace ui::agenda::page
{
namespace
{
using NoteSnapshot = TextSnapshot<sizeof(::agenda::EventRecord::note) - 1>;
template <std::size_t Capacity>
bool restoreText(lv_obj_t* field, const TextSnapshot<Capacity>& snapshot)
{
    lv_textarea_set_text(field, "");
    return snapshot.forEachUtf8([field](const char* scalar)
                                {
                                   lv_textarea_add_text(field, scalar);
                                   return true; });
}
} // namespace
struct Flow::ReturnContext
{
    AgendaEditorModel draft;
    ::agenda::Occurrence detail_occurrence;
    ::ui::map::MapLocationRequest location;
    ::ui::map::MapTargetRequest target;
    ::ui::page::Host navigation;
    AgendaRequest agenda_request{};
    ::agenda::CivilTime picker_date{};
    int32_t time_values[4]{};
    // LVGL limits characters, while EventRecord limits UTF-8 bytes. Keep live
    // text verbatim across interruptions, including input not yet saveable.
    TextSnapshot<sizeof(::agenda::EventRecord::title) - 1> live_title;
    NoteSnapshot live_note;
    NoteSnapshot modal_text;
    ::ui::widgets::ImeEditState modal_state{};
    uint8_t modal_field = 0; // 0: none, 1: title, 2: note.
    const char* error = nullptr;
    uint32_t pending_sequence = 0;
    uint32_t waypoint_after = 0;
    View restore_view = View::Agenda;
    bool live_text = false;
    bool suspended = false;
    bool awaiting_command = false;
    bool confirm_delete = false;
    bool confirm_discard = false;
    bool picker_jump = false;
    bool time_has_end = false;
    bool prepared = false;
    bool return_detail = false;
    bool entered = false;
    bool returning = false;
    bool show_target = false;
    bool failed = false;
};

Flow::Flow(Host& host) : host_(host)
{
    // Preserve the embedded limit while accounting for native pointer widths.
    static_assert(sizeof(ReturnContext) < (sizeof(void*) == 4 ? 1024 : 1152), "UI recovery context exceeded its architecture-specific budget");
    host_.map_context = this;
    host_.request_map = requestMap;
    host_.request_target = requestTarget;
}
Flow::~Flow()
{
    close();
    host_.map_context = nullptr;
    host_.request_map = nullptr;
    host_.request_target = nullptr;
}
void Flow::enter(void* context, lv_obj_t* parent)
{
    auto& self = *static_cast<Flow*>(context);
    if (self.parent_ || !parent) return;
    if (!self.return_ && !self.reserve_)
    {
        // Reserve bounded recovery storage before accepting any user input.
        // A later call must not lose a draft because allocation is exhausted.
        self.reserve_ = new (std::nothrow) ReturnContext{};
        if (!self.reserve_) return;
    }
    self.parent_ = parent;
    if (self.return_) self.return_->suspended = false;
    if (!self.return_) runtime::enter(&self.host_, parent);
}
void Flow::exit(void* context, lv_obj_t*)
{
    auto& self = *static_cast<Flow*>(context);
    if (self.parent_ && ui_is_interruption_app_active()) self.suspend();
    else self.close();
}
void Flow::suspend()
{
    if (!return_) return_ = captureReturn();
    if (!return_)
    {
        close(); // Allocation failure cannot keep a second LVGL tree alive.
        return;
    }
    if (return_->entered) ::gps::ui::shell::exit_route(nullptr, parent_);
    else runtime::exit();
    return_->entered = false;
    return_->prepared = false;
    return_->returning = true;
    return_->suspended = true;
    parent_ = nullptr;
}
void Flow::close()
{
    if (parent_)
    {
        if (return_ && return_->entered) ::gps::ui::shell::exit_route(nullptr, parent_);
        else runtime::exit();
    }
    delete return_;
    delete reserve_;
    return_ = nullptr;
    reserve_ = nullptr;
    parent_ = nullptr;
}
bool Flow::requestMap(void* context, const AgendaEditorModel& draft, bool return_detail)
{
    return static_cast<Flow*>(context)->queueMap(draft, return_detail, false);
}
bool Flow::requestTarget(void* context, const AgendaEditorModel& draft)
{
    return static_cast<Flow*>(context)->queueMap(draft, true, true);
}
bool Flow::queueMap(const AgendaEditorModel& draft, bool return_detail, bool show_target)
{
    if (!parent_ || return_ || !::gps::ui::runtime::is_available()) return false;
    if (show_target && (!(draft.event.flags & ::agenda::HasLocation) ||
                        !state() || state()->view != View::Detail)) return false;
    auto* pending = captureReturn();
    if (!pending) return false;
    pending->draft = draft;
    if (const auto* current = state()) pending->detail_occurrence = current->detail_occurrence;
    pending->return_detail = return_detail;
    pending->navigation = {this, requestReturn};
    pending->show_target = show_target;
    pending->restore_view = show_target ? View::Detail : View::Editor;
    if (draft.event.flags & ::agenda::HasLocation)
    {
        pending->location.has_initial_viewport = true;
        pending->location.initial_viewport.center_lat = draft.event.latitude_e7 / 1e7;
        pending->location.initial_viewport.center_lon = draft.event.longitude_e7 / 1e7;
    }
    if (show_target)
    {
        pending->target.viewport = pending->location.initial_viewport;
        ::ui::copyText(pending->target.label, draft.event.location_name);
        if (!pending->target.valid())
        {
            reserve_ = pending;
            return false;
        }
    }
    return_ = pending;
    return true; // Never destroy the caller's widgets inside its event/timer.
}
Flow::ReturnContext* Flow::captureReturn()
{
    auto* pending = reserve_;
    reserve_ = nullptr;
    if (pending)
    {
        pending->~ReturnContext();
        new (pending) ReturnContext{}; // Reset in place; no large stack temporary.
    }
    else pending = new (std::nothrow) ReturnContext{};
    if (!pending) return nullptr;
    pending->navigation = {this, requestReturn};
    pending->agenda_request.visible_rows = 0; // Closed Agenda returns to today's default list.
    if (parent_ && state())
    {
        const auto& s = *state();
        pending->draft = s.draft;
        pending->detail_occurrence = s.detail_occurrence;
        pending->return_detail = s.editor_return == View::Detail;
        pending->restore_view = s.view;
        pending->agenda_request = s.request;
        pending->picker_date = s.picker.date;
        pending->picker_jump = s.picker.jump;
        pending->error = s.error;
        pending->pending_sequence = s.pending_sequence;
        pending->waypoint_after = s.waypoint_page.after;
        pending->awaiting_command = s.awaiting_command;
        pending->confirm_delete = s.confirm_delete;
        pending->confirm_discard = s.confirm_discard;
        if ((s.view == View::Editor || s.view == View::WaypointName) && s.editor_widgets.title)
        {
            const char* title = lv_textarea_get_text(s.editor_widgets.title);
            const char* note = s.editor_widgets.note ? lv_textarea_get_text(s.editor_widgets.note) : "";
            // The bounds follow the textareas' configured character limits.
            // Reject an unexpected larger value rather than truncate bytes.
            if (!pending->live_title.assign(title) || !pending->live_note.assign(note))
            {
                reserve_ = pending;
                return nullptr;
            }
            pending->live_text = true;
            lv_obj_t* fields[] = {s.editor_widgets.title, s.editor_widgets.note};
            for (unsigned i = 0; i < 2; ++i)
            {
                if (!fields[i]) continue;
                const auto captured = ::ui::widgets::capture_touch_text_editor(
                    fields[i], [](void* context, const char* text)
                    { return static_cast<NoteSnapshot*>(context)->assign(text); },
                    &pending->modal_text, pending->modal_state);
                if (captured == ::ui::widgets::TouchEditorCapture::InsufficientCapacity)
                {
                    reserve_ = pending;
                    return nullptr;
                }
                if (captured == ::ui::widgets::TouchEditorCapture::Captured)
                {
                    pending->modal_field = i + 1;
                    break;
                }
            }
        }
        if (s.view == View::TimePicker)
        {
            for (unsigned i = 0; i < 4; ++i)
                pending->time_values[i] = lv_spinbox_get_value(s.editor_widgets.clock_fields[i]);
            pending->time_has_end = lv_obj_has_state(s.editor_widgets.end_enabled, LV_STATE_CHECKED);
        }
    }
    return pending;
}
const char* Flow::prepareDestination(int32_t latitude_e7, int32_t longitude_e7)
{
    if (return_ || ui_is_transition_pending() || ui_is_interruption_app_active()) return "Could not update reminder";
    if (!::gps::ui::runtime::is_available()) return "Map is unavailable on this target.";
    if (latitude_e7 < -900000000 || latitude_e7 > 900000000 ||
        longitude_e7 < -1800000000 || longitude_e7 > 1800000000) return "Invalid location";
    if (parent_)
    {
        if (!state() || !canPresentReminder()) return "Could not update reminder";
        if (!editor::captureText()) return state()->error;
    }
    auto* pending = captureReturn();
    if (!pending) return "Map is unavailable on this target.";
    pending->show_target = true;
    pending->prepared = true;
    pending->target.viewport.center_lat = latitude_e7 / 1e7;
    pending->target.viewport.center_lon = longitude_e7 / 1e7;
    return_ = pending;
    return nullptr;
}
void Flow::finishDestination(bool accepted)
{
    if (!return_ || !return_->prepared) return;
    if (accepted) return_->prepared = false;
    else
    {
        if (parent_) reserve_ = return_;
        else delete return_;
        return_ = nullptr;
    }
}
bool Flow::needsActivation() const
{
    return return_ && !return_->prepared && !return_->suspended && !parent_;
}
void Flow::discardSuspended()
{
    if (return_ && return_->suspended && !parent_) close();
}
void Flow::invalidateStorage()
{
    auto* parent = parent_;
    close();
    // Rebuild only this flow's active page. Never resurrect a suspended page
    // over another application, or preserve IDs/drafts from the old medium.
    if (parent) enter(this, parent);
}
void Flow::requestReturn(void* context)
{
    auto& self = *static_cast<Flow*>(context);
    if (self.return_) self.return_->returning = true;
}
void Flow::restoreEditor()
{
    // Map must release its entire tree and input group before re-entering Agenda.
    if (return_->entered) ::gps::ui::shell::exit_route(nullptr, parent_);
    return_->entered = false;
    runtime::enter(&host_, parent_);
    auto* s = state();
    if (!s) return; // Retain the bounded draft and retry after allocation pressure.
    s->draft = return_->draft;
    s->detail_occurrence = return_->detail_occurrence;
    s->editor_return = return_->return_detail ? View::Detail : View::Agenda;
    s->view = return_->restore_view;
    s->error = return_->error;
    s->pending_sequence = return_->pending_sequence;
    s->waypoint_page.after = return_->waypoint_after;
    s->awaiting_command = return_->awaiting_command;
    s->confirm_delete = return_->confirm_delete;
    if (return_->agenda_request.visible_rows)
    {
        s->request = return_->agenda_request;
        host_.source->snapshot(s->request, s->snapshot);
    }
    s->picker.date = return_->picker_date;
    s->picker.jump = return_->picker_jump;
    const auto& result = return_->location.result;
    if (!return_->show_target && result.state == ::ui::map::MapLocationSelectionState::Picked)
    {
        AgendaCoordinate coordinate;
        if (!coordinateFromDegrees(result.latitude, result.longitude, coordinate) ||
            !applyCoordinate(s->draft, coordinate)) s->error = "Invalid location";
    }
    if (return_->failed)
        s->error = "Map is unavailable on this target.";
    components::render();
    if ((s->view == View::Editor || s->view == View::WaypointName) && return_->live_text)
    {
        restoreText(s->editor_widgets.title, return_->live_title);
        ::ui::fonts::apply_content_font(s->editor_widgets.title, lv_textarea_get_text(s->editor_widgets.title), ::ui::page_profile::resolve_body_font());
        if (s->editor_widgets.note)
        {
            restoreText(s->editor_widgets.note, return_->live_note);
            ::ui::fonts::apply_content_font(s->editor_widgets.note, lv_textarea_get_text(s->editor_widgets.note), ::ui::page_profile::resolve_body_font());
        }
    }
    if (return_->confirm_discard)
    {
        // First reconstruct live fields, then place confirmation above them.
        s->confirm_discard = true;
        components::render();
    }
    if (s->view == View::TimePicker)
    {
        for (unsigned i = 0; i < 4; ++i)
            lv_spinbox_set_value(s->editor_widgets.clock_fields[i], return_->time_values[i]);
        if (return_->time_has_end) lv_obj_add_state(s->editor_widgets.end_enabled, LV_STATE_CHECKED);
        else lv_obj_remove_state(s->editor_widgets.end_enabled, LV_STATE_CHECKED);
    }
    if ((s->view == View::Editor || s->view == View::WaypointName) && return_->modal_field)
    {
        auto* source = return_->modal_field == 1 ? s->editor_widgets.title : s->editor_widgets.note;
        if (!::ui::widgets::restore_touch_text_editor(
                source, return_->modal_state, [](const void* context, lv_obj_t* field)
                { return restoreText(field, *static_cast<const NoteSnapshot*>(context)); },
                &return_->modal_text)) return;
    }
    // Reuse this allocation for the next interruption while the page is open.
    // Never retain both a return context and an additional recovery reserve.
    reserve_ = return_;
    return_ = nullptr;
}
void Flow::tick()
{
    if (!return_ || return_->prepared || !parent_ || ui_is_interruption_app_active() || ui_is_transition_pending()) return;
    if (return_->returning)
    {
        restoreEditor();
        return;
    }
    if (return_->entered || ui_is_overlay_active()) return;
    runtime::exit();
    // Wait for the old touch/key release before giving the Map its controls.
    for (auto* input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input))
        lv_indev_wait_release(input);
    ::gps::ui::shell::RouteSpec route{&return_->navigation, ::gps::ui::shell::Projection::Map,
                                      return_->show_target ? nullptr : &return_->location,
                                      return_->show_target ? &return_->target : nullptr};
    ::gps::ui::shell::enter_route(&route, parent_);
    return_->entered = true;
    if (return_->show_target ? !return_->target.entered
                             : return_->location.result.state != ::ui::map::MapLocationSelectionState::Selecting)
    {
        return_->failed = true;
        return_->returning = true;
    }
}
} // namespace ui::agenda::page

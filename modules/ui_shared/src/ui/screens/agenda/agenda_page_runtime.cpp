#include "ui/screens/agenda/agenda_page_runtime.h"
#include "ui/page/page_profile.h"
#include "ui/screens/agenda/agenda_editor_runtime.h"
#include "ui/screens/agenda/agenda_page_components.h"
#include "ui/screens/agenda/agenda_page_input.h"
#include "ui_presentation/agenda/agenda_waypoint.h"
#include "ui_presentation/waypoint/waypoint_model.h"

namespace ui::agenda::page::runtime
{
namespace
{
void load_today()
{
    auto& s = *state();
    s.host->source->snapshot(s.request, s.snapshot);
    s.request.day_start = s.snapshot.today_start;
    s.request.after = {};
    s.host->source->snapshot(s.request, s.snapshot);
}

void tick(lv_timer_t*)
{
    auto* s = state();
    if (!s) return;
    components::updateCountdowns();
    if (s->awaiting_command && s->view == View::WaypointName)
    {
        const auto result = s->host->waypoints->commandResult();
        if (result.sequence != s->pending_sequence || result.state == ::ui::waypoint::CommandState::Pending) return;
        s->awaiting_command = false;
        ::waypoint::Record saved;
        if (result.state == ::ui::waypoint::CommandState::Succeeded &&
            s->host->waypoints->detail(result.id, saved) == ::waypoint::Result::Ok && applyWaypoint(s->draft, saved))
        {
            s->view = View::Editor;
            components::render();
        }
        else
        {
            s->error = "Could not save changes";
            editor::showStatus(); // Keep the entered name for retry.
        }
        return;
    }
    if (s->awaiting_command)
    {
        const auto result = s->host->source->commandResult();
        if (result.sequence != s->pending_sequence || result.state == CommandState::Pending) return;
        s->awaiting_command = false;
        if (result.state == CommandState::Succeeded)
        {
            s->view = View::Agenda;
            s->confirm_delete = false;
            load_today();
        }
        else s->error = "Could not save changes";
        components::render();
    }
    const Action action = s->pending;
    s->pending = Action::None;
    if (action == Action::None) return;
    s->error = nullptr;
    const auto transition = editor::handle(action, s->action_value);
    if (transition != editor::Transition::Unhandled)
    {
        if (transition == editor::Transition::Render) components::render();
        else editor::showStatus();
        return;
    }
    switch (action)
    {
    case Action::Back:
        if (s->confirm_delete) s->confirm_delete = false;
        else if (s->view == View::Detail) s->view = View::Agenda;
        else
        {
            ::ui::page::request_exit(&s->host->navigation);
            return; // Host may have destroyed this page.
        }
        break;
    case Action::Today:
        load_today();
        s->view = View::Agenda;
        break;
    case Action::NextPage:
        if (s->snapshot.page.has_more && s->snapshot.page.count)
        {
            const auto& last = s->snapshot.page.rows[s->snapshot.page.count - 1].occurrence;
            s->request.after = {last.start, last.event_id, true};
            s->host->source->snapshot(s->request, s->snapshot);
        }
        break;
    case Action::OpenRow:
        if (s->selected_row < s->snapshot.page.count)
        {
            const auto id = s->snapshot.page.rows[s->selected_row].occurrence.event_id;
            if (s->host->source->detail(id, s->draft.event) == ::agenda::AgendaResult::Ok)
            {
                s->detail_occurrence = s->snapshot.page.rows[s->selected_row].occurrence;
                s->view = View::Detail;
            }
            else s->error = "Event is unavailable";
        }
        break;
    case Action::Navigate:
        if (s->view != View::Detail || !(s->draft.event.flags & ::agenda::HasLocation)) break;
        if (!s->host->request_target || !s->host->request_target(s->host->map_context, s->draft))
            s->error = "Map is unavailable on this target.";
        break;
    case Action::Delete:
        s->confirm_delete = true;
        break;
    case Action::CancelDelete:
        s->confirm_delete = false;
        break;
    case Action::ConfirmDelete:
        if (s->host->actions->remove(s->draft.event.id).ok)
        {
            s->pending_sequence = s->host->source->commandResult().sequence;
            s->awaiting_command = true;
        }
        else s->error = "Could not save changes";
        break;
    default:
        break;
    }
    components::render();
}
} // namespace

void enter(const Host* host, lv_obj_t* parent)
{
    if (!host || !host->source || !host->actions || !parent || state() || !createState()) return;
    auto& s = *state();
    s.host = host;
    input::begin();
    // Both geometries bound the snapshot to the actual visible event budget.
    s.request.visible_rows = ::ui::page_profile::current().variant ==
                                     ::ui::page_profile::LayoutVariant::EncoderCompact
                                 ? 5
                                 : 4;
    load_today();
    components::create(parent);
    components::render();
    s.timer = lv_timer_create(tick, 50, nullptr);
}
void exit()
{
    auto* s = state();
    if (!s) return;
    if (s->timer) lv_timer_delete(s->timer);
    s->timer = nullptr;
    input::end();
    components::destroy();
    destroyState();
}
void request(Action action, uint8_t row)
{
    auto* s = state();
    if (!s || s->awaiting_command || s->pending != Action::None) return;
    s->pending = action;
    s->action_value = row;
    if (action == Action::OpenRow) s->selected_row = row;
}
void refresh()
{
    auto* s = state();
    if (!s || s->view != View::Agenda || s->awaiting_command) return;
    s->host->source->snapshot(s->request, s->snapshot);
    components::render();
}
} // namespace ui::agenda::page::runtime

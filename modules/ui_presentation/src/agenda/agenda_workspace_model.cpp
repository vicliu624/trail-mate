#include "ui_presentation/agenda/agenda_workspace_model.h"
#include <algorithm>
#include <cstring>
#include <new>

namespace ui::agenda
{
ui::map::MapMarkerStatus AgendaWorkspaceModel::markerStatus() const
{
    const auto time = clock_.sample();
    return {time.calendar_seconds, revision_, ready_, time.valid};
}

bool AgendaWorkspaceModel::visitMarkers(ui::map::MapMarkerVisitor visitor, void* context,
                                        void* scratch, std::size_t scratch_bytes)
{
    if (!ready_ || !visitor || !scratch || scratch_bytes < sizeof(::agenda::EventRecord)) return false;
    auto* record = new (scratch)::agenda::EventRecord{};
    const auto time = clock_.sample();
    bool success = true;
    for (uint16_t slot = 0; slot < store_.slotCount(); ++slot)
    {
        if (store_.readSlot(slot, *record) != ::agenda::StoreResult::Ok)
        {
            success = false;
            break;
        }
        if (record->state != ::agenda::RecordState::Active || !(record->flags & ::agenda::HasLocation)) continue;
        ui::map::MapMarker marker{};
        marker.id = record->id;
        marker.latitude_e7 = record->latitude_e7;
        marker.longitude_e7 = record->longitude_e7;
        std::memcpy(marker.title, record->title, sizeof(marker.title));
        marker.title[sizeof(marker.title) - 1] = '\0';
        marker.active_until = (record->flags & ::agenda::HasEndTime) ? record->end_time : record->start_time;
        if (record->repeat != ::agenda::Repeat::None && time.valid)
        {
            ::agenda::Occurrence occurrence;
            const auto duration = marker.active_until - record->start_time;
            if (::agenda::nextOccurrence(*record, std::max<int64_t>(0, time.calendar_seconds - duration), occurrence))
                marker.active_until = occurrence.has_end ? occurrence.end : occurrence.start;
        }
        visitor(marker, context);
    }
    record->~EventRecord();
    return success;
}

void AgendaWorkspaceModel::snapshot(const AgendaRequest& request, AgendaSnapshot& out)
{
    out = {};
    const auto time = clock_.sample();
    out.clock_valid = time.valid;
    out.storage_ready = ready_;
    out.revision = revision_;
    if (time.valid) out.today_start = time.calendar_seconds / 86400 * 86400;
    if (!ready_)
    {
        out.result = ::agenda::StoreResult::IoError;
        return;
    }
    if (!time.valid) return;
    // Upcoming is streamed through the same bounded cursor, not cut off after
    // an arbitrary week or month. The renderer labels sections relative to today.
    out.result = ::agenda::queryAgenda(store_, request.day_start, ::agenda::kLastCalendarSecond + 1,
                                       request.visible_rows, request.after, out.page);
}

::agenda::AgendaResult AgendaWorkspaceModel::detail(uint32_t id, ::agenda::EventRecord& out)
{
    out = {};
    return ready_ ? service_.read(id, out) : ::agenda::AgendaResult::StorageError;
}

bool AgendaWorkspaceModel::newDraft(AgendaEditorModel& out) const
{
    out = {};
    if (!ready_) return false;
    const auto time = clock_.sample();
    if (!time.valid || time.calendar_seconds < 0 || time.calendar_seconds > ::agenda::kLastCalendarSecond - 60)
        return false;
    out.event.start_time = (time.calendar_seconds / 60 + 1) * 60;
    out.event.state = ::agenda::RecordState::Active;
    return true;
}

void AgendaWorkspaceModel::month(uint16_t year, uint8_t month, MonthSnapshot& out)
{
    out = {};
    out.year = year;
    out.month = month;
    out.result = ready_ ? ::agenda::queryMonth(store_, year, month, out.occupied_days) : ::agenda::StoreResult::IoError;
}

UiActionResult AgendaWorkspaceModel::enqueue(Command command)
{
    if (!ready_) return UiActionResult::fail(UiActionFailure::NotReady);
    if (command_ != Command::None) return UiActionResult::fail(UiActionFailure::Busy);
    command_ = command;
    ++result_.sequence;
    result_.state = CommandState::Pending;
    result_.result = ::agenda::AgendaResult::Ok;
    result_.event_id = 0;
    return UiActionResult::success();
}

UiActionResult AgendaWorkspaceModel::save(const ::agenda::EventRecord& draft)
{
    // Validate without altering the caller's draft or using a second record.
    if (!ready_) return UiActionResult::fail(UiActionFailure::NotReady);
    if (command_ != Command::None) return UiActionResult::fail(UiActionFailure::Busy);
    pending_ = draft;
    pending_.state = ::agenda::RecordState::Active;
    if (!pending_.id) pending_.id = 1;
    if (!::agenda::validEvent(pending_))
    {
        pending_ = {};
        return UiActionResult::fail(UiActionFailure::InvalidInput);
    }
    pending_.id = draft.id;
    return enqueue(Command::Save);
}

UiActionResult AgendaWorkspaceModel::remove(uint32_t event_id)
{
    if (event_id == 0) return UiActionResult::fail(UiActionFailure::InvalidInput);
    const auto result = enqueue(Command::Delete);
    if (result.ok)
    {
        pending_ = {};
        pending_.id = event_id;
    }
    return result;
}

bool AgendaWorkspaceModel::matchesReminder(uint32_t revision) const
{
    const auto current = reminders_.reminderSnapshot();
    return revision != 0 && current.reminder.valid && current.revision == revision && scheduler_.showing();
}

UiActionResult AgendaWorkspaceModel::enqueueReminder(Command command, uint32_t revision)
{
    if (!matchesReminder(revision)) return UiActionResult::fail(UiActionFailure::InvalidInput);
    const auto result = enqueue(command);
    if (result.ok)
    {
        pending_ = {};
        // The single command slot is reused; no second pending action buffer.
        pending_.id = revision;
    }
    return result;
}

UiActionResult AgendaWorkspaceModel::dismissReminder(uint32_t revision)
{
    return enqueueReminder(Command::Dismiss, revision);
}
UiActionResult AgendaWorkspaceModel::snoozeReminder(uint32_t revision)
{
    return enqueueReminder(Command::Snooze, revision);
}

void AgendaWorkspaceModel::pump()
{
    if (!ready_)
    {
        if (command_ != Command::None)
        {
            result_.state = CommandState::Failed;
            result_.result = ::agenda::AgendaResult::StorageError;
            command_ = Command::None;
            pending_ = {};
        }
        return;
    }
    if (command_ != Command::None)
    {
        bool changed = false;
        result_.event_id = pending_.id;
        switch (command_)
        {
        case Command::Save:
            result_.result = pending_.id ? service_.update(pending_) : service_.create(pending_, result_.event_id);
            changed = result_.result == ::agenda::AgendaResult::Ok;
            break;
        case Command::Delete:
            result_.result = service_.remove(pending_.id);
            changed = result_.result == ::agenda::AgendaResult::Ok;
            break;
        case Command::Dismiss:
            result_.event_id = 0;
            result_.result = matchesReminder(pending_.id) && scheduler_.dismiss()
                                 ? ::agenda::AgendaResult::Ok
                                 : ::agenda::AgendaResult::NotFound;
            break;
        case Command::Snooze:
            result_.event_id = 0;
            result_.result = !matchesReminder(pending_.id) ? ::agenda::AgendaResult::NotFound
                             : scheduler_.snooze()         ? ::agenda::AgendaResult::Ok
                                                           : ::agenda::AgendaResult::Full;
            break;
        default:
            break;
        }
        result_.state = result_.result == ::agenda::AgendaResult::Ok ? CommandState::Succeeded : CommandState::Failed;
        pending_ = {};
        command_ = Command::None;
        if (changed)
        {
            ++revision_;
            scheduler_.eventsChanged();
        }
    }
    scheduler_.tick();
}
} // namespace ui::agenda

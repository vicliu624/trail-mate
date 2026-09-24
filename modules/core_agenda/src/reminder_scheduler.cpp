#include "agenda/usecase/reminder_scheduler.h"

#include "agenda/domain/recurrence.h"

#include <algorithm>

namespace agenda
{
namespace
{
bool earlier(const Reminder& a, const Reminder& b)
{
    return a.trigger_time < b.trigger_time ||
           (a.trigger_time == b.trigger_time && a.event_id < b.event_id);
}
} // namespace

void ReminderScheduler::scan(int64_t now)
{
    next_ = {};
    status_ = StoreResult::Ok;
    dirty_ = false;
    if (store_.slotCount() > kMaxActiveEvents)
    {
        status_ = StoreResult::OutOfRange;
        return;
    }
    EventRecord event;
    bool snooze_exists = !snoozed_.valid;
    for (uint16_t slot = 0; slot < store_.slotCount(); ++slot)
    {
        const auto result = store_.readSlot(slot, event);
        if (result != StoreResult::Ok)
        {
            status_ = result;
            // Corrupt slots do not disable healthy reminders; I/O failure is
            // reported, without hammering flash again on every normal tick.
            continue;
        }
        if (event.state != RecordState::Active || !(event.flags & HasReminder)) continue;
        Occurrence occurrence;
        if (snoozed_.valid && event.id == snoozed_.event_id &&
            nextOccurrence(event, snoozed_.occurrence_start, occurrence) &&
            occurrence.start == snoozed_.occurrence_start)
            snooze_exists = true;
        int64_t lower = floor_;
        if (consumed_.valid && consumed_.trigger_time >= lower)
            lower = consumed_.trigger_time + (event.id <= consumed_.event_id ? 1 : 0);
        if (lower > kLastCalendarSecond - event.reminder_offset_sec) continue;
        if (!nextOccurrence(event, lower + event.reminder_offset_sec, occurrence)) continue;
        Reminder candidate;
        candidate.event_id = event.id;
        candidate.occurrence_start = occurrence.start;
        candidate.trigger_time = occurrence.start - event.reminder_offset_sec;
        candidate.valid = true;
        if (!next_.valid || earlier(candidate, next_)) next_ = candidate;
    }
    if (!snooze_exists) snoozed_ = {};
    (void)now;
}

void ReminderScheduler::eventsChanged()
{
    if (showing_)
    {
        sink_.withdraw();
        showing_ = false;
    }
    dirty_ = true;
}

void ReminderScheduler::tick()
{
    const auto current = clock_.sample();
    if (!current.valid || current.calendar_seconds < 0 || current.calendar_seconds > kLastCalendarSecond)
    {
        if (showing_) sink_.withdraw();
        showing_ = false;
        previous_ = current;
        dirty_ = true;
        return;
    }
    if (!initialized_)
    {
        initialized_ = true;
        floor_ = current.calendar_seconds;
    }
    else if (!previous_.valid)
    {
        floor_ = current.calendar_seconds;
        dirty_ = true;
    }
    else
    {
        const int64_t wall_delta = current.calendar_seconds - previous_.calendar_seconds;
        const uint64_t monotonic_delta = current.monotonic_seconds - previous_.monotonic_seconds;
        if (current.revision != previous_.revision || current.monotonic_seconds < previous_.monotonic_seconds ||
            monotonic_delta > static_cast<uint64_t>(kLastCalendarSecond) ||
            wall_delta < static_cast<int64_t>(monotonic_delta) - 2 ||
            wall_delta > static_cast<int64_t>(monotonic_delta) + 2)
        {
            // Time corrections do not replay the entire missed calendar.
            floor_ = current.calendar_seconds;
            dirty_ = true;
        }
    }
    previous_ = current;
    if (showing_) return;
    if (dirty_) scan(current.calendar_seconds);
    if (snoozed_.valid && current.monotonic_seconds >= snooze_deadline_)
    {
        showing_snooze_ = true;
        showing_ = sink_.present(snoozed_);
    }
    else if (next_.valid && current.calendar_seconds >= next_.trigger_time)
    {
        showing_snooze_ = false;
        showing_ = sink_.present(next_);
    }
}

void ReminderScheduler::consume()
{
    if (showing_snooze_) snoozed_ = {};
    else consumed_ = next_;
    showing_ = false;
    sink_.withdraw();
    dirty_ = true;
}

bool ReminderScheduler::dismiss()
{
    if (!showing_) return false;
    consume();
    return true;
}

bool ReminderScheduler::snooze()
{
    if (!showing_ || (snoozed_.valid && !showing_snooze_)) return false;
    const auto reminder = showing_snooze_ ? snoozed_ : next_;
    consume();
    snoozed_ = reminder;
    snooze_deadline_ = previous_.monotonic_seconds + 600;
    return true;
}
} // namespace agenda

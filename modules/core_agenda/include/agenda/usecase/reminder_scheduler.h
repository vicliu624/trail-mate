#pragma once

#include "agenda/ports/agenda_clock.h"
#include "agenda/ports/agenda_store.h"
#include "agenda/ports/reminder_sink.h"

namespace agenda
{

// Next reminder + at most one explicit snooze, never a queue of future events.
// Startup ignores already-past triggers. Dismissal consumes only one occurrence.
// All calls belong to the same composition-owned runtime, not the Agenda page.
class ReminderScheduler
{
  public:
    ReminderScheduler(IAgendaStore& store, IAgendaClock& clock, IReminderSink& sink)
        : store_(store), clock_(clock), sink_(sink) {}
    void tick();
    void eventsChanged();
    // Media identity/lifetime boundary: no reminder state may cross it.
    void resetStorage()
    {
        sink_.withdraw();
        next_ = {};
        snoozed_ = {};
        consumed_ = {};
        previous_ = {};
        floor_ = 0;
        snooze_deadline_ = 0;
        status_ = StoreResult::Ok;
        initialized_ = false;
        dirty_ = true;
        showing_ = false;
        showing_snooze_ = false;
    }
    bool dismiss();
    bool snooze();
    bool showing() const { return showing_; }
    StoreResult status() const { return status_; }

  private:
    void scan(int64_t now);
    void consume();
    IAgendaStore& store_;
    IAgendaClock& clock_;
    IReminderSink& sink_;
    Reminder next_{};
    Reminder snoozed_{};
    Reminder consumed_{};
    ClockSample previous_{};
    int64_t floor_ = 0;
    uint64_t snooze_deadline_ = 0;
    StoreResult status_ = StoreResult::Ok;
    bool initialized_ = false;
    bool dirty_ = true;
    bool showing_ = false;
    bool showing_snooze_ = false;
};

static_assert(sizeof(ReminderScheduler) < 256, "Reminder scheduler exceeded preferred RAM budget");
} // namespace agenda

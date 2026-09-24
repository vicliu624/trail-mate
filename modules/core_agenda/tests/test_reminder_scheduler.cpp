#include "agenda/usecase/reminder_scheduler.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace
{
class Clock final : public agenda::IAgendaClock
{
  public:
    agenda::ClockSample value{100000, 0, 0, true};
    agenda::ClockSample sample() const override { return value; }
    void advance(uint64_t seconds)
    {
        value.calendar_seconds += static_cast<int64_t>(seconds);
        value.monotonic_seconds += seconds;
    }
};

class Sink final : public agenda::IReminderSink
{
  public:
    agenda::Reminder shown;
    unsigned presentations = 0;
    bool busy = false;
    bool present(const agenda::Reminder& reminder) override
    {
        if (busy) return false;
        ++presentations;
        shown = reminder;
        return true;
    }
    void withdraw() override { shown = {}; }
};

class Store final : public agenda::IAgendaStore
{
  public:
    agenda::EventRecord events[3]{};
    unsigned reads = 0;
    agenda::StoreResult readSlot(uint16_t slot, agenda::EventRecord& out) override
    {
        ++reads;
        out = events[slot];
        return agenda::StoreResult::Ok;
    }
    agenda::StoreResult writeSlot(uint16_t, const agenda::EventRecord&) override { return agenda::StoreResult::IoError; }
    agenda::StoreResult eraseSlot(uint16_t) override { return agenda::StoreResult::IoError; }
    uint16_t slotCount() const override { return 3; }
};

agenda::EventRecord event(uint32_t id, int64_t start)
{
    agenda::EventRecord value;
    value.id = id;
    value.state = agenda::RecordState::Active;
    value.start_time = start;
    value.flags = agenda::HasReminder;
    value.reminder_offset_sec = 600;
    std::strcpy(value.title, "Radio check");
    return value;
}
} // namespace

int main()
{
    using namespace agenda;
    Store store;
    Clock clock;
    Sink sink;
    store.events[0] = event(2, 101200);
    store.events[1] = event(1, 101200);
    store.events[1].flags |= HasLocation;
    store.events[1].location_type = LocationType::Coordinate;
    store.events[2] = event(3, 101000);
    store.events[2].flags = 0; // No reminder is a valid ordinary event.
    ReminderScheduler scheduler(store, clock, sink);
    scheduler.tick();
    assert(store.reads == 3 && !scheduler.showing());
    for (unsigned i = 0; i < 599; ++i)
    {
        clock.advance(1);
        scheduler.tick();
    }
    assert(store.reads == 3 && sink.presentations == 0);
    clock.advance(1);
    sink.busy = true;
    scheduler.tick();
    assert(!scheduler.showing() && store.reads == 3);
    sink.busy = false;
    scheduler.tick();
    assert(scheduler.showing() && sink.shown.event_id == 1);
    scheduler.tick();
    assert(sink.presentations == 1); // Never duplicate a visible popup.
    assert(scheduler.snooze());
    scheduler.tick();
    assert(scheduler.showing() && sink.shown.event_id == 2); // Same-time tie not lost.
    assert(!scheduler.snooze());                             // Do not silently replace an existing snooze.
    assert(scheduler.dismiss());
    scheduler.tick();
    const unsigned reads = store.reads;
    clock.advance(599);
    scheduler.tick();
    assert(!scheduler.showing() && store.reads == reads);
    clock.advance(1);
    scheduler.tick();
    assert(scheduler.showing() && sink.shown.event_id == 1);
    assert(scheduler.dismiss());
    scheduler.tick();
    assert(!scheduler.showing());

    // New and edited events cause scans, even while the Agenda page is absent.
    store.events[0] = event(4, clock.value.calendar_seconds + 600);
    scheduler.eventsChanged();
    scheduler.tick();
    assert(scheduler.showing() && sink.shown.event_id == 4);
    store.events[0].state = RecordState::Deleted;
    scheduler.eventsChanged();
    assert(!scheduler.showing());
    scheduler.tick();
    assert(!scheduler.showing());

    store.events[0] = event(5, clock.value.calendar_seconds + 1000);
    scheduler.eventsChanged();
    scheduler.tick();
    const unsigned before_jump = store.reads;
    clock.value.calendar_seconds += 10000; // Material wall-clock correction.
    scheduler.tick();
    assert(store.reads == before_jump + 3 && !scheduler.showing());

    // Startup does not replay old triggers; daily series advances after Done.
    store.events[0] = event(6, clock.value.calendar_seconds + 600);
    store.events[0].repeat = Repeat::Daily;
    ReminderScheduler restarted(store, clock, sink);
    restarted.tick();
    assert(restarted.showing() && sink.shown.event_id == 6);
    assert(restarted.dismiss());
    restarted.tick();
    clock.advance(86400);
    restarted.tick();
    assert(restarted.showing() && sink.shown.occurrence_start == store.events[0].start_time + 86400);
    assert(restarted.snooze());
    store.events[0].state = RecordState::Deleted;
    restarted.eventsChanged();
    restarted.tick();
    clock.advance(600);
    restarted.tick();
    assert(!restarted.showing());

    clock.value.valid = false;
    restarted.tick();
    const unsigned invalid_reads = store.reads;
    restarted.tick();
    assert(store.reads == invalid_reads);
    std::printf("ReminderScheduler=%zu Reminder=%zu ClockSample=%zu\n",
                sizeof(ReminderScheduler), sizeof(Reminder), sizeof(ClockSample));
}

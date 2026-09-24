#include "agenda/domain/recurrence.h"
#include "agenda/usecase/reminder_scheduler.h"

#include <cassert>
#include <cstring>

namespace
{
class Clock final : public agenda::IAgendaClock
{
  public:
    agenda::ClockSample value{1000, 0, 0, true};
    agenda::ClockSample sample() const override { return value; }
};
class Store final : public agenda::IAgendaStore
{
  public:
    agenda::EventRecord record;
    agenda::StoreResult readSlot(uint16_t, agenda::EventRecord& out) override
    {
        out = record;
        return agenda::StoreResult::Ok;
    }
    agenda::StoreResult writeSlot(uint16_t, const agenda::EventRecord&) override { return agenda::StoreResult::IoError; }
    agenda::StoreResult eraseSlot(uint16_t) override { return agenda::StoreResult::IoError; }
    uint16_t slotCount() const override { return 1; }
};
class Sink final : public agenda::IReminderSink
{
  public:
    bool present(const agenda::Reminder&) override { return true; }
    void withdraw() override {}
};
} // namespace

int main()
{
    using namespace agenda;
    Store store;
    auto& record = store.record;
    record.id = 1;
    record.state = RecordState::Active;
    std::strcpy(record.title, "Radio check");
    record.start_time = kLastCalendarSecond;
    assert(validEvent(record));
    record.start_time++;
    assert(!validEvent(record));
    record.start_time = 1000;
    record.flags = HasEndTime;
    record.end_time = kLastCalendarSecond + 1;
    assert(!validEvent(record));
    record.end_time = kLastCalendarSecond;
    assert(validEvent(record));

    record.flags = HasReminder;
    record.start_time = 1100;
    Clock clock;
    Sink sink;
    ReminderScheduler scheduler(store, clock, sink);
    scheduler.tick();
    assert(!scheduler.showing());
    clock.value.valid = false;
    scheduler.tick();
    clock.value.calendar_seconds = 1200;
    clock.value.monotonic_seconds = 200;
    clock.value.valid = true;
    scheduler.tick();
    assert(!scheduler.showing()); // No stale popup when time authority returns.
    record.start_time = 1300;
    scheduler.eventsChanged();
    scheduler.tick();
    clock.value.calendar_seconds = 1300;
    clock.value.monotonic_seconds = 300;
    scheduler.tick();
    assert(scheduler.showing());
}

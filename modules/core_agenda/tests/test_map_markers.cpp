#include "ui_presentation/agenda/agenda_workspace_model.h"
#include "ui_presentation/map/map_marker_cache.h"
#include <cassert>
#include <cstdio>
#include <cstring>

namespace
{
class Store final : public agenda::IAgendaStore
{
  public:
    agenda::EventRecord records[64]{};
    unsigned reads = 0;
    int fail_slot = -1;
    agenda::StoreResult readSlot(uint16_t slot, agenda::EventRecord& out) override
    {
        ++reads;
        if (slot == fail_slot) return agenda::StoreResult::IoError;
        out = records[slot];
        return agenda::StoreResult::Ok;
    }
    agenda::StoreResult writeSlot(uint16_t slot, const agenda::EventRecord& value) override
    {
        records[slot] = value;
        return agenda::StoreResult::Ok;
    }
    agenda::StoreResult eraseSlot(uint16_t) override { return agenda::StoreResult::IoError; }
    uint16_t slotCount() const override { return 64; }
};
class Clock final : public agenda::IAgendaClock
{
  public:
    agenda::ClockSample value{100000, 0, 0, true};
    agenda::ClockSample sample() const override { return value; }
};
void collect(const ui::map::MapMarker& marker, void* context)
{
    static_cast<ui::map::MapMarkerCache*>(context)->offer(marker, marker.id);
}
} // namespace

int main()
{
    Store store;
    Clock clock;
    ui::agenda::AgendaReminderModel reminders;
    agenda::AgendaService service(store);
    agenda::ReminderScheduler scheduler(store, clock, reminders);
    ui::agenda::AgendaWorkspaceModel model(store, service, clock, scheduler, reminders);
    ui::map::MapMarkerCache cache;
    assert(!model.visitMarkers(collect, &cache, cache.scratch, sizeof(cache.scratch)));
    assert(store.reads == 0);
    model.setStorageReady(true);
    for (unsigned i = 0; i < 64; ++i)
    {
        auto& event = store.records[i];
        event.id = 64 - i; // Reverse storage order must not affect selection.
        event.state = agenda::RecordState::Active;
        event.flags = agenda::HasLocation;
        event.location_type = agenda::LocationType::Coordinate;
        event.latitude_e7 = 300000000;
        event.longitude_e7 = 1200000000;
        event.start_time = 90000 + i * 1000;
        std::strcpy(event.title, "Map event");
    }
    assert(model.visitMarkers(collect, &cache, cache.scratch, sizeof(cache.scratch)));
    assert(store.reads == 64 && cache.count == 32 && cache.truncated);
    assert(cache.entries[0].marker.id == 1 && cache.entries[31].marker.id == 32);
    cache.commit(model.markerStatus());
    assert(!cache.stale(model.markerStatus()));
    assert(store.reads == 64); // Status and cache hits never access storage.
    clock.value.valid = false;
    assert(cache.stale(model.markerStatus()));
    clock.value.valid = true;
    --clock.value.calendar_seconds;
    assert(cache.stale(model.markerStatus()));
    ++clock.value.calendar_seconds;
    model.setStorageReady(false);
    assert(cache.stale(model.markerStatus()));
    model.setStorageReady(true);
    assert(cache.stale(model.markerStatus()));

    for (auto& event : store.records) event.state = agenda::RecordState::Empty;
    auto& event = store.records[0];
    event.id = 1;
    event.state = agenda::RecordState::Active;
    event.start_time = 90000;
    cache.reset();
    assert(model.visitMarkers(collect, &cache, cache.scratch, sizeof(cache.scratch)));
    assert(cache.count == 1 && cache.entries[0].marker.active_until < clock.value.calendar_seconds);
    event.flags |= agenda::HasEndTime;
    event.end_time = 110000;
    cache.reset();
    assert(model.visitMarkers(collect, &cache, cache.scratch, sizeof(cache.scratch)));
    assert(cache.entries[0].marker.active_until == 110000); // Ongoing stays red.
    cache.commit(model.markerStatus());
    clock.value.calendar_seconds = 110001;
    assert(cache.stale(model.markerStatus()));
    event.repeat = agenda::Repeat::Daily;
    cache.reset();
    assert(model.visitMarkers(collect, &cache, cache.scratch, sizeof(cache.scratch)));
    assert(cache.count == 1 && cache.entries[0].marker.active_until > clock.value.calendar_seconds);
    event.state = agenda::RecordState::Deleted;
    cache.reset();
    assert(model.visitMarkers(collect, &cache, cache.scratch, sizeof(cache.scratch)) && cache.count == 0);
    event.state = agenda::RecordState::Active;
    event.flags = 0;
    cache.reset();
    assert(model.visitMarkers(collect, &cache, cache.scratch, sizeof(cache.scratch)) && cache.count == 0);
    store.fail_slot = 20;
    assert(!model.visitMarkers(collect, &cache, cache.scratch, sizeof(cache.scratch)));
    assert(!model.visitMarkers(collect, &cache, cache.scratch, 1));
    std::printf("MapMarker=%zu cache=%zu scratch=%zu; no persistent event mirror\n",
                sizeof(ui::map::MapMarker), sizeof(cache), sizeof(cache.scratch));
}

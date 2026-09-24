#include "ui_presentation/agenda/agenda_workspace_model.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace
{
class Store final : public agenda::IAgendaStore
{
  public:
    agenda::EventRecord slots[2]{};
    unsigned writes = 0;
    bool fail = false;
    agenda::StoreResult readSlot(uint16_t slot, agenda::EventRecord& out) override
    {
        out = slots[slot];
        return agenda::StoreResult::Ok;
    }
    agenda::StoreResult writeSlot(uint16_t slot, const agenda::EventRecord& event) override
    {
        ++writes;
        if (fail) return agenda::StoreResult::IoError;
        slots[slot] = event;
        return agenda::StoreResult::Ok;
    }
    agenda::StoreResult eraseSlot(uint16_t) override { return agenda::StoreResult::IoError; }
    uint16_t slotCount() const override { return 2; }
};
class Clock final : public agenda::IAgendaClock
{
  public:
    agenda::ClockSample value{100000, 0, 0, true};
    agenda::ClockSample sample() const override { return value; }
};
} // namespace

int main()
{
    using namespace ui::agenda;
    Store store;
    Clock clock;
    AgendaReminderModel sink;
    ::agenda::AgendaService service(store);
    ::agenda::ReminderScheduler scheduler(store, clock, sink);
    AgendaWorkspaceModel model(store, service, clock, scheduler, sink);
    AgendaEditorModel draft;
    assert(!model.newDraft(draft));
    assert(!model.save(draft.event).ok && store.writes == 0);
    model.setStorageReady(true);
    assert(model.newDraft(draft));
    assert(draft.event.id == 0 && draft.event.start_time == 100020);
    assert(!model.save(draft.event).ok); // Required title.
    std::strcpy(draft.event.title, "Radio check");
    assert(model.save(draft.event).ok);
    assert(store.writes == 0 && model.commandResult().state == CommandState::Pending);
    assert(model.remove(123).failure == ui::UiActionFailure::Busy);
    // The command owns a copy; later UI edits cannot mutate a queued save.
    std::strcpy(draft.event.title, "Unsaved edit");
    model.pump();
    assert(store.writes == 1 && model.commandResult().state == CommandState::Succeeded);
    assert(model.commandResult().event_id == 1);
    ::agenda::EventRecord detail;
    assert(model.detail(1, detail) == ::agenda::AgendaResult::Ok);
    assert(std::strcmp(detail.title, "Radio check") == 0);

    AgendaSnapshot snapshot;
    AgendaRequest request;
    request.day_start = 86400;
    model.snapshot(request, snapshot);
    assert(snapshot.clock_valid && snapshot.storage_ready && snapshot.page.count == 1);
    assert(snapshot.revision == 2 && snapshot.today_start == 86400);
    MonthSnapshot month;
    model.month(1970, 1, month);
    assert(month.occupied_days == 2); // January 2.

    store.fail = true;
    detail.flags = ::agenda::HasNote;
    std::strcpy(detail.note, "New note");
    assert(model.save(detail).ok);
    model.pump();
    assert(model.commandResult().state == CommandState::Failed);
    assert(model.commandResult().result == ::agenda::AgendaResult::StorageError);
    assert(model.detail(1, detail) == ::agenda::AgendaResult::Ok && detail.note[0] == '\0');
    store.fail = false;
    assert(model.remove(1).ok);
    assert(model.detail(1, detail) == ::agenda::AgendaResult::Ok);
    model.pump();
    assert(model.detail(1, detail) == ::agenda::AgendaResult::NotFound);

    // A write queued before storage becomes unavailable must terminate Failed.
    assert(model.save(draft.event).ok);
    model.setStorageReady(false);
    model.pump();
    assert(model.commandResult().state == CommandState::Failed);
    model.snapshot(request, snapshot);
    assert(!snapshot.storage_ready && snapshot.page.count == 0);
    clock.value.valid = false;
    assert(!model.newDraft(draft));
    std::printf("AgendaWorkspaceModel=%zu AgendaSnapshot=%zu EditorDraft=%zu AgendaService=%zu\n",
                sizeof(AgendaWorkspaceModel), sizeof(AgendaSnapshot), sizeof(AgendaEditorModel), sizeof(service));
}

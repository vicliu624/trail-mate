#include "platform/esp/common/storage/agenda_file_store.h"
#include "product_composition/agenda_composition.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace
{
class Store final : public agenda::IAgendaStore
{
  public:
    agenda::EventRecord slots[2]{}; // Test-only in-memory store.
    unsigned reads = 0;
    agenda::StoreResult readSlot(uint16_t slot, agenda::EventRecord& out) override
    {
        ++reads;
        out = slots[slot];
        return agenda::StoreResult::Ok;
    }
    agenda::StoreResult writeSlot(uint16_t slot, const agenda::EventRecord& event) override
    {
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
    void advance(uint32_t seconds)
    {
        value.calendar_seconds += seconds;
        value.monotonic_seconds += seconds;
    }
};

// Match the production ownership shape, including adapter memory rather than
// testing only the scheduler in isolation. Clock's test sample storage is also
// charged here (the production clock adapter itself is stateless).
struct TargetOwnedAgenda
{
    platform::esp::storage::AgendaFileStore store{"unused", "unused.tmp"};
    Clock clock;
    product_composition::AgendaComposition composition{store, clock};
};
static_assert(sizeof(TargetOwnedAgenda) < 1024, "Total idle Agenda runtime exceeded 1 KiB");

void test_reminder_bridge()
{
    ui::agenda::AgendaReminderModel bridge;
    agenda::Reminder reminder{101000, 100400, 1, true};
    assert(!bridge.present(reminder)); // No live shell yet.
    bridge.setDeliveryEnabled(true);
    assert(bridge.present(reminder));
    const auto first = bridge.reminderSnapshot();
    assert(first.reminder.valid && first.revision != 0);
    assert(!bridge.present(reminder)); // Cannot overwrite an unhandled reminder.
    bridge.setDeliveryEnabled(false);
    assert(bridge.reminderSnapshot().revision == first.revision);
    bridge.withdraw();
    assert(!bridge.reminderSnapshot().reminder.valid);
    assert(bridge.reminderSnapshot().revision != first.revision);
    const auto cleared = bridge.reminderSnapshot().revision;
    bridge.withdraw();
    assert(bridge.reminderSnapshot().revision == cleared);
}

void test_page_independent_lifecycle()
{
    Store store;
    Clock clock;
    product_composition::AgendaComposition composition(store, clock);
    ui::workspace::PresentationWorkspace workspace;
    assert(!workspace.hasAgenda());
    assert(!ui::workspace::hasInteractiveWorkspaceModels(workspace));
    composition.bind(workspace);
    assert(workspace.hasAgenda());
    assert(ui::workspace::hasInteractiveWorkspaceModels(workspace));
    composition.setStorageReady(true);
    auto& model = *workspace.agenda;
    auto& reminders = *workspace.agenda_reminders;
    // The page/draft ends its lifetime before any reminder is delivered.
    {
        ui::agenda::AgendaEditorModel draft;
        assert(model.newDraft(draft));
        std::strcpy(draft.event.title, "Radio check");
        draft.event.start_time = clock.value.calendar_seconds + 660;
        draft.event.reminder_offset_sec = 600;
        draft.event.flags = agenda::HasReminder; // No location needed.
        assert(model.save(draft.event).ok);
        composition.tick(true);
        assert(model.commandResult().state == ui::agenda::CommandState::Succeeded);
        // A second event at the same trigger exercises serial presentation.
        std::strcpy(draft.event.title, "Check battery");
        assert(model.save(draft.event).ok);
        composition.tick(true);
    }
    const auto reads = store.reads;
    for (unsigned i = 0; i < 60; ++i)
    {
        clock.advance(1);
        composition.tick(false); // Another modal owns input.
        assert(!reminders.reminderSnapshot().reminder.valid);
    }
    assert(store.reads == reads); // No per-tick flash scan or retry queue.
    composition.tick(true);
    auto first = reminders.reminderSnapshot();
    assert(first.reminder.valid && first.reminder.event_id == 1);
    assert(!model.dismissReminder(0).ok);
    assert(model.snoozeReminder(first.revision).ok);
    assert(reminders.reminderSnapshot().revision == first.revision); // Deferred action.
    composition.tick(true);
    assert(model.commandResult().state == ui::agenda::CommandState::Succeeded);
    auto second = reminders.reminderSnapshot();
    assert(second.reminder.valid && second.reminder.event_id == 2);
    assert(second.revision != first.revision);
    assert(!model.dismissReminder(first.revision).ok); // Old popup cannot dismiss the next event.
    assert(model.snoozeReminder(second.revision).ok);
    composition.tick(true);
    assert(model.commandResult().state == ui::agenda::CommandState::Failed);
    assert(model.commandResult().result == agenda::AgendaResult::Full);
    assert(reminders.reminderSnapshot().revision == second.revision); // No silent replacement.
    assert(model.dismissReminder(second.revision).ok);
    composition.tick(true);
    assert(!reminders.reminderSnapshot().reminder.valid);
    clock.advance(600);
    composition.tick(true);
    auto snoozed = reminders.reminderSnapshot();
    assert(snoozed.reminder.valid && snoozed.reminder.event_id == 1);
    assert(snoozed.revision != first.revision);
    assert(!model.dismissReminder(first.revision).ok); // Same ID/occurrence, new presentation.
    assert(model.dismissReminder(snoozed.revision).ok);
    composition.tick(true);
    assert(!reminders.reminderSnapshot().reminder.valid);
}

void test_queued_action_invalidated()
{
    Store store;
    Clock clock;
    agenda::AgendaService service(store);
    ui::agenda::AgendaReminderModel reminders;
    agenda::ReminderScheduler scheduler(store, clock, reminders);
    ui::agenda::AgendaWorkspaceModel model(store, service, clock, scheduler, reminders);
    model.setStorageReady(true);
    reminders.setDeliveryEnabled(true);
    auto& event = store.slots[0];
    event.state = agenda::RecordState::Active;
    event.id = 1;
    std::strcpy(event.title, "Due now");
    event.start_time = clock.value.calendar_seconds;
    event.flags = agenda::HasReminder;
    model.pump();
    const auto first = reminders.reminderSnapshot();
    assert(first.reminder.valid);
    assert(model.dismissReminder(first.revision).ok);
    // Model action was accepted, but the reminder was withdrawn before pump.
    scheduler.eventsChanged();
    scheduler.tick();
    const auto replacement = reminders.reminderSnapshot();
    assert(replacement.reminder.valid && replacement.revision != first.revision);
    model.pump();
    assert(model.commandResult().state == ui::agenda::CommandState::Failed);
    assert(model.commandResult().result == agenda::AgendaResult::NotFound);
    assert(reminders.reminderSnapshot().revision == replacement.revision);
}
} // namespace

void test_storage_lifetime_boundary()
{
    Store store;
    Clock clock;
    product_composition::AgendaComposition composition(store, clock);
    ui::workspace::PresentationWorkspace workspace;
    composition.bind(workspace);
    auto& model = *workspace.agenda;
    auto& reminders = *workspace.agenda_reminders;
    ui::agenda::AgendaEditorModel draft;
    assert(!model.newDraft(draft));
    composition.setStorageReady(true);
    assert(model.newDraft(draft));
    std::strcpy(draft.event.title, "Must not cross cards");
    assert(model.save(draft.event).ok);
    composition.setStorageReady(false);
    assert(model.commandResult().state == ui::agenda::CommandState::Failed);
    composition.setStorageReady(true); // No pump between removal and insertion.
    composition.tick(true);
    assert(store.slots[0].id == 0 && store.slots[1].id == 0);

    draft.event.start_time = clock.value.calendar_seconds;
    draft.event.flags = agenda::HasReminder;
    assert(model.save(draft.event).ok);
    composition.tick(true);
    auto shown = reminders.reminderSnapshot();
    assert(shown.reminder.valid);
    assert(model.snoozeReminder(shown.revision).ok);
    composition.tick(true);
    composition.setStorageReady(false);
    assert(!reminders.reminderSnapshot().reminder.valid);
    store.slots[0] = {}; // Replacement medium contains no events.
    composition.setStorageReady(true);
    clock.advance(600);
    composition.tick(true);
    assert(!reminders.reminderSnapshot().reminder.valid);
    assert(!model.dismissReminder(shown.revision).ok);
}

int main()
{
    test_reminder_bridge();
    test_page_independent_lifecycle();
    test_queued_action_invalidated();
    test_storage_lifetime_boundary();
    std::printf("AgendaComposition=%zu ReminderBridge=%zu TargetOwnedAgenda=%zu\n",
                sizeof(product_composition::AgendaComposition), sizeof(ui::agenda::AgendaReminderModel),
                sizeof(TargetOwnedAgenda));
}

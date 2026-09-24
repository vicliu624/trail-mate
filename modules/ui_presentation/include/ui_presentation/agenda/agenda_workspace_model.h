#pragma once

#include "agenda/ports/agenda_clock.h"
#include "agenda/usecase/reminder_scheduler.h"
#include "ui_presentation/agenda/agenda_action_sink.h"
#include "ui_presentation/agenda/agenda_reminder_source.h"
#include "ui_presentation/agenda/agenda_source.h"
#include "ui_presentation/map/map_marker_source.h"

namespace ui::agenda
{
// Constructed and pumped by the target composition. Requests enqueue a single
// bounded command; durable writes happen from pump(), outside LVGL callbacks.
class AgendaWorkspaceModel final : public IAgendaSource, public IAgendaActionSink, public ui::map::IMapMarkerSource
{
  public:
    AgendaWorkspaceModel(::agenda::IAgendaStore& store, ::agenda::AgendaService& service,
                         ::agenda::IAgendaClock& clock, ::agenda::ReminderScheduler& scheduler,
                         const IAgendaReminderSource& reminders)
        : store_(store), service_(service), clock_(clock), scheduler_(scheduler), reminders_(reminders) {}
    void setStorageReady(bool ready)
    {
        if (ready_ != ready) ++revision_;
        ready_ = ready;
        if (!ready)
        {
            // Cancel now, even if another card becomes ready before pump().
            if (command_ != Command::None)
            {
                result_.state = CommandState::Failed;
                result_.result = ::agenda::AgendaResult::StorageError;
                result_.event_id = 0;
            }
            command_ = Command::None;
            pending_ = {};
            scheduler_.resetStorage();
        }
    }
    void pump();
    ui::map::MapMarkerStatus markerStatus() const override;
    bool visitMarkers(ui::map::MapMarkerVisitor visitor, void* context,
                      void* scratch, std::size_t scratch_bytes) override;
    ::agenda::ClockSample currentTime() const override { return clock_.sample(); }
    void snapshot(const AgendaRequest& request, AgendaSnapshot& out) override;
    ::agenda::AgendaResult detail(uint32_t id, ::agenda::EventRecord& out) override;
    bool newDraft(AgendaEditorModel& out) const override;
    void month(uint16_t year, uint8_t month, MonthSnapshot& out) override;
    CommandResult commandResult() const override { return result_; }
    UiActionResult save(const ::agenda::EventRecord& draft) override;
    UiActionResult remove(uint32_t event_id) override;
    UiActionResult dismissReminder(uint32_t reminder_revision) override;
    UiActionResult snoozeReminder(uint32_t reminder_revision) override;

  private:
    enum class Command : uint8_t
    {
        None,
        Save,
        Delete,
        Dismiss,
        Snooze
    };
    UiActionResult enqueue(Command command);
    UiActionResult enqueueReminder(Command command, uint32_t revision);
    bool matchesReminder(uint32_t revision) const;
    ::agenda::IAgendaStore& store_;
    ::agenda::AgendaService& service_;
    ::agenda::IAgendaClock& clock_;
    ::agenda::ReminderScheduler& scheduler_;
    const IAgendaReminderSource& reminders_;
    ::agenda::EventRecord pending_{};
    CommandResult result_{};
    uint32_t revision_ = 0;
    Command command_ = Command::None;
    bool ready_ = false;
};

} // namespace ui::agenda

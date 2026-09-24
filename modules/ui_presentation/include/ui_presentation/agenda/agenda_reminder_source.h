#pragma once

#include "agenda/ports/reminder_sink.h"

namespace ui::agenda
{
struct ReminderSnapshot
{
    ::agenda::Reminder reminder{};
    // A popup action must name the presentation it was rendered from. Event ID
    // alone is insufficient for repeated events and snoozed occurrences.
    uint32_t revision = 0;
};

class IAgendaReminderSource
{
  public:
    virtual ~IAgendaReminderSource() = default;
    virtual ReminderSnapshot reminderSnapshot() const = 0;
};

// Composition-owned bridge, not a popup or event cache. Renderer and scheduler
// are pumped on the same owner thread. Delivery is enabled only after the shell
// can present reminders; another modal may temporarily disable new deliveries.
class AgendaReminderModel final : public ::agenda::IReminderSink, public IAgendaReminderSource
{
  public:
    void setDeliveryEnabled(bool enabled) { delivery_enabled_ = enabled; }
    ReminderSnapshot reminderSnapshot() const override { return current_; }
    bool present(const ::agenda::Reminder& reminder) override
    {
        if (!delivery_enabled_ || !reminder.valid || reminder.event_id == 0 || current_.reminder.valid)
            return false;
        current_.reminder = reminder;
        advanceRevision();
        return true;
    }
    void withdraw() override
    {
        if (!current_.reminder.valid) return;
        current_.reminder = {};
        advanceRevision();
    }

  private:
    void advanceRevision()
    {
        if (++current_.revision == 0) ++current_.revision;
    }
    ReminderSnapshot current_{};
    bool delivery_enabled_ = false;
};

static_assert(sizeof(AgendaReminderModel) <= 64, "Reminder bridge must not cache an event record");
} // namespace ui::agenda

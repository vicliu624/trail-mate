#pragma once

#include "ui_presentation/agenda/agenda_workspace_model.h"
#include "ui_presentation/workspace/presentation_workspace.h"

namespace product_composition
{
// An owned component of an enabled target's composition root, never a global
// service locator or a page singleton. The target owns store/clock adapters and
// calls tick from its normal application loop, even while Calendar is closed.
class AgendaComposition
{
  public:
    AgendaComposition(::agenda::IAgendaStore& store, ::agenda::IAgendaClock& clock)
        : service_(store), scheduler_(store, clock, reminders_),
          model_(store, service_, clock, scheduler_, reminders_) {}
    AgendaComposition(const AgendaComposition&) = delete;
    AgendaComposition& operator=(const AgendaComposition&) = delete;

    void bind(ui::workspace::PresentationWorkspace& workspace)
    {
        workspace.agenda = &model_;
        workspace.agenda_reminders = &reminders_;
    }
    void setStorageReady(bool ready)
    {
        model_.setStorageReady(ready);
        if (!ready) reminders_.setDeliveryEnabled(false);
    }
    void tick(bool can_present_reminder)
    {
        reminders_.setDeliveryEnabled(can_present_reminder);
        model_.pump();
    }

  private:
    ::agenda::AgendaService service_;
    ui::agenda::AgendaReminderModel reminders_;
    ::agenda::ReminderScheduler scheduler_;
    ui::agenda::AgendaWorkspaceModel model_;
};

static_assert(sizeof(AgendaComposition) <= 512, "Leave at least half the idle budget for storage and clock");
} // namespace product_composition

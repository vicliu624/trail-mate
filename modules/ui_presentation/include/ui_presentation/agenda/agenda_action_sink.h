#pragma once

#include "agenda/domain/event.h"
#include "ui_presentation/common/ui_action_result.h"

namespace ui::agenda
{
class IAgendaActionSink
{
  public:
    virtual ~IAgendaActionSink() = default;
    // Success means accepted, not persisted. Observe commandResult before
    // closing the editor or reporting success. A second command returns Busy.
    virtual UiActionResult save(const ::agenda::EventRecord& draft) = 0;
    virtual UiActionResult remove(uint32_t event_id) = 0;
    virtual UiActionResult dismissReminder(uint32_t reminder_revision) = 0;
    virtual UiActionResult snoozeReminder(uint32_t reminder_revision) = 0;
};
} // namespace ui::agenda

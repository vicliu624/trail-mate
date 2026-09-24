#pragma once
#include "agenda/ports/agenda_clock.h"

#include "ui_presentation/agenda/agenda_editor_model.h"
#include "ui_presentation/agenda/agenda_snapshot.h"

namespace ui::agenda
{
class IAgendaSource
{
  public:
    virtual ~IAgendaSource() = default;
    virtual ::agenda::ClockSample currentTime() const { return {}; }
    virtual void snapshot(const AgendaRequest& request, AgendaSnapshot& out) = 0;
    virtual ::agenda::AgendaResult detail(uint32_t id, ::agenda::EventRecord& out) = 0;
    virtual bool newDraft(AgendaEditorModel& out) const = 0;
    virtual void month(uint16_t year, uint8_t month, MonthSnapshot& out) = 0;
    virtual CommandResult commandResult() const = 0;
};
} // namespace ui::agenda

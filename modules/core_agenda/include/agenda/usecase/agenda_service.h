#pragma once

#include "agenda/ports/agenda_store.h"

namespace agenda
{

enum class AgendaResult : uint8_t
{
    Ok,
    Invalid,
    NotFound,
    Full,
    StorageError,
    IdExhausted,
};

// Single-owner service. One bounded operation-local record, no resident table. The caller
// retains its draft until the durable write succeeds. IDs never change on edit.
class AgendaService
{
  public:
    explicit AgendaService(IAgendaStore& store) : store_(store) {}
    AgendaResult create(const EventRecord& draft, uint32_t& created_id);
    AgendaResult update(const EventRecord& event);
    AgendaResult remove(uint32_t id);
    AgendaResult read(uint32_t id, EventRecord& out);

  private:
    AgendaResult find(uint32_t id, uint16_t& slot, EventRecord& scratch_);
    IAgendaStore& store_;
};

static_assert(sizeof(AgendaService) == sizeof(void*), "Agenda service must not retain records");

} // namespace agenda

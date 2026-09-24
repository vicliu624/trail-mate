#pragma once

#include "agenda/domain/recurrence.h"
#include "agenda/ports/agenda_store.h"

namespace agenda
{

constexpr uint8_t kMaxAgendaRows = 10;
struct AgendaEntry
{
    Occurrence occurrence;
    char title[40]{};
    char location[32]{};
    uint8_t flags = 0;
};

struct AgendaPage
{
    AgendaEntry rows[kMaxAgendaRows]{};
    uint8_t count = 0;
    bool has_more = false;
    bool degraded = false;
};
static_assert(sizeof(AgendaPage) < 2048, "Agenda query snapshot exceeded RAM budget");

struct QueryCursor
{
    int64_t start = 0;
    uint32_t event_id = 0;
    bool valid = false;
};

// [begin,end), sorted by (occurrence start,event id), strictly after cursor.
// Caller selects a capacity based on visible geometry. No hidden full table.
StoreResult queryAgenda(IAgendaStore& store, int64_t begin, int64_t end,
                        uint8_t capacity, const QueryCursor& after, AgendaPage& out);
StoreResult queryMonth(IAgendaStore& store, uint16_t year, uint8_t month, uint32_t& occupied_days);

} // namespace agenda

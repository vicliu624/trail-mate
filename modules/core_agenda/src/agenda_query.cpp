#include "agenda/usecase/agenda_query.h"

#include <cstring>

namespace agenda
{
namespace
{
bool before(const Occurrence& left, const Occurrence& right)
{
    return left.start < right.start || (left.start == right.start && left.event_id < right.event_id);
}
} // namespace

StoreResult queryAgenda(IAgendaStore& store, int64_t begin, int64_t end,
                        uint8_t capacity, const QueryCursor& after, AgendaPage& out)
{
    out = {};
    if (capacity == 0 || capacity > kMaxAgendaRows || begin < 0 || end <= begin ||
        end > kLastCalendarSecond + 1 || store.slotCount() > kMaxActiveEvents)
        return StoreResult::OutOfRange;
    EventRecord event;
    for (uint16_t slot = 0; slot < store.slotCount(); ++slot)
    {
        const auto status = store.readSlot(slot, event);
        if (status == StoreResult::Corrupt || status == StoreResult::InvalidRecord)
        {
            out.degraded = true;
            continue;
        }
        if (status != StoreResult::Ok)
        {
            out = {};
            return status;
        }
        int64_t lower = begin;
        if (after.valid && after.start >= lower)
        {
            if (after.start >= end) continue;
            lower = after.start + (event.id <= after.event_id ? 1 : 0);
        }
        Occurrence occurrence;
        // At most capacity+1 occurrences per persistent event are needed to
        // determine the top N and the continuation bit, even for distant dates.
        for (uint8_t n = 0; n <= capacity && nextOccurrence(event, lower, occurrence); ++n)
        {
            if (occurrence.start >= end) break;
            uint8_t index = 0;
            while (index < out.count && !before(occurrence, out.rows[index].occurrence)) ++index;
            if (out.count == capacity) out.has_more = true;
            if (index == capacity) break;
            if (out.count < capacity) ++out.count;
            for (uint8_t pos = out.count - 1; pos > index; --pos) out.rows[pos] = out.rows[pos - 1];
            auto& row = out.rows[index];
            row = {};
            row.occurrence = occurrence;
            row.flags = event.flags;
            std::memcpy(row.title, event.title, sizeof(row.title));
            if (event.flags & HasLocation) std::memcpy(row.location, event.location_name, sizeof(row.location));
            lower = occurrence.start + 1;
        }
    }
    return StoreResult::Ok;
}

StoreResult queryMonth(IAgendaStore& store, uint16_t year, uint8_t month, uint32_t& occupied_days)
{
    occupied_days = 0;
    CivilTime first;
    first.year = year;
    first.month = month;
    int64_t begin = 0;
    if (!toCalendarSeconds(first, begin) || store.slotCount() > kMaxActiveEvents) return StoreResult::OutOfRange;
    const int64_t end = begin + daysInMonth(year, month) * 86400;
    EventRecord event;
    for (uint16_t slot = 0; slot < store.slotCount(); ++slot)
    {
        const auto status = store.readSlot(slot, event);
        if (status != StoreResult::Ok)
        {
            occupied_days = 0;
            return status;
        }
        int64_t lower = begin;
        Occurrence occurrence;
        while (nextOccurrence(event, lower, occurrence) && occurrence.start < end)
        {
            const auto day_index = static_cast<uint8_t>((occurrence.start - begin) / 86400);
            occupied_days |= uint32_t{1} << day_index;
            lower = occurrence.start + 1;
        }
    }
    return StoreResult::Ok;
}

} // namespace agenda

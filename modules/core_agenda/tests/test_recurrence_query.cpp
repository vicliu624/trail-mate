#include "agenda/usecase/agenda_query.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

namespace
{
int64_t at(uint16_t year, uint8_t month, uint8_t day, uint8_t hour = 9)
{
    agenda::CivilTime civil;
    civil.year = year;
    civil.month = month;
    civil.day = day;
    civil.hour = hour;
    int64_t result = 0;
    assert(agenda::toCalendarSeconds(civil, result));
    return result;
}

agenda::EventRecord event(uint32_t id, int64_t start, agenda::Repeat repeat = agenda::Repeat::None)
{
    agenda::EventRecord result;
    result.id = id;
    result.start_time = start;
    result.repeat = repeat;
    result.state = agenda::RecordState::Active;
    std::strcpy(result.title, "Radio check");
    return result;
}

class Store final : public agenda::IAgendaStore
{
  public:
    agenda::EventRecord records[agenda::kMaxActiveEvents]{};
    int corrupt = -1;
    agenda::StoreResult readSlot(uint16_t slot, agenda::EventRecord& out) override
    {
        if (slot == corrupt) return agenda::StoreResult::Corrupt;
        out = records[slot];
        return agenda::StoreResult::Ok;
    }
    agenda::StoreResult writeSlot(uint16_t, const agenda::EventRecord&) override { return agenda::StoreResult::IoError; }
    agenda::StoreResult eraseSlot(uint16_t) override { return agenda::StoreResult::IoError; }
    uint16_t slotCount() const override { return agenda::kMaxActiveEvents; }
};
} // namespace

int main()
{
    using namespace agenda;
    // Round-trip every civil date across ordinary, leap and non-leap centuries.
    for (uint16_t year = 1970; year <= 2400; ++year)
        for (uint8_t month = 1; month <= 12; ++month)
            for (uint8_t day = 1; day <= daysInMonth(year, month); ++day)
            {
                CivilTime value;
                assert(fromCalendarSeconds(at(year, month, day), value));
                assert(value.year == year && value.month == month && value.day == day && value.hour == 9);
            }
    CivilTime civil;
    assert(fromCalendarSeconds(0, civil) && civil.year == 1970 && civil.day == 1);
    assert(fromCalendarSeconds(kLastCalendarSecond, civil) && civil.year == 9999 && civil.day == 31);
    assert(!fromCalendarSeconds(-1, civil));
    assert(!fromCalendarSeconds(kLastCalendarSecond + 1, civil));
    assert(daysInMonth(2000, 2) == 29 && daysInMonth(2100, 2) == 28);

    auto single = event(1, at(2026, 9, 21));
    Occurrence occurrence;
    assert(nextOccurrence(single, 0, occurrence) && occurrence.start == single.start_time && !occurrence.has_end);
    assert(nextOccurrence(single, single.start_time, occurrence));
    assert(!nextOccurrence(single, single.start_time + 1, occurrence));
    single.flags = HasEndTime;
    single.end_time = single.start_time + 3600;
    single.repeat = Repeat::Daily;
    assert(nextOccurrence(single, single.start_time + 1, occurrence));
    assert(occurrence.start == at(2026, 9, 22) && occurrence.end == at(2026, 9, 22, 10));
    single.repeat = Repeat::Weekly;
    assert(nextOccurrence(single, single.start_time + 1, occurrence) && occurrence.start == at(2026, 9, 28));

    auto monthly = event(2, at(2024, 1, 31), Repeat::Monthly);
    assert(nextOccurrence(monthly, at(2024, 2, 1), occurrence) && occurrence.start == at(2024, 3, 31));
    assert(nextOccurrence(monthly, at(2024, 4, 1), occurrence) && occurrence.start == at(2024, 5, 31));
    auto yearly = event(3, at(2096, 2, 29), Repeat::Yearly);
    assert(nextOccurrence(yearly, yearly.start_time + 1, occurrence) && occurrence.start == at(2104, 2, 29));
    assert(!nextOccurrence(yearly, std::numeric_limits<int64_t>::max(), occurrence));
    yearly.start_time = at(9999, 12, 31);
    assert(!nextOccurrence(yearly, yearly.start_time + 1, occurrence));

    Store store;
    const int64_t today = at(2026, 9, 21, 0);
    store.records[0] = event(9, today + 3600);
    store.records[1] = event(4, today + 3600);
    store.records[2] = event(7, today + 86400 + 3600);
    store.records[3] = event(8, today + 2 * 86400 + 3600);
    AgendaPage page;
    QueryCursor cursor;
    assert(queryAgenda(store, today, today + 86400, 10, cursor, page) == StoreResult::Ok);
    assert(page.count == 2 && !page.has_more && page.rows[0].occurrence.event_id == 4);
    assert(page.rows[1].occurrence.event_id == 9 && page.rows[0].location[0] == '\0');
    assert(queryAgenda(store, today + 86400, today + 2 * 86400, 10, cursor, page) == StoreResult::Ok);
    assert(page.count == 1 && page.rows[0].occurrence.event_id == 7);
    assert(queryAgenda(store, today + 2 * 86400, today + 7 * 86400, 10, cursor, page) == StoreResult::Ok);
    assert(page.count == 1 && page.rows[0].occurrence.event_id == 8);

    // Equal-time records paginate without skips despite reverse slot order.
    for (uint16_t i = 0; i < 64; ++i) store.records[i] = event(64 - i, today + 3600);
    uint32_t expected = 1;
    do
    {
        assert(queryAgenda(store, today, today + 86400, 10, cursor, page) == StoreResult::Ok);
        assert(page.count > 0 && page.count <= 10);
        for (uint8_t i = 0; i < page.count; ++i) assert(page.rows[i].occurrence.event_id == expected++);
        const auto& last = page.rows[page.count - 1].occurrence;
        cursor = {last.start, last.event_id, true};
    } while (page.has_more);
    assert(expected == 65);
    assert(queryAgenda(store, today, today + 86400, 11, {}, page) == StoreResult::OutOfRange);
    store.corrupt = 0;
    assert(queryAgenda(store, today, today + 86400, 10, {}, page) == StoreResult::Ok && page.degraded);
    store.corrupt = -1;
    for (auto& record : store.records) record = {};
    store.records[0] = event(1, at(2026, 9, 1), Repeat::Daily);
    uint32_t days = 0;
    assert(queryMonth(store, 2026, 9, days) == StoreResult::Ok && days == 0x3fffffffU);
    assert(queryAgenda(store, today, today + 30 * 86400, 10, {}, page) == StoreResult::Ok);
    assert(page.count == 10 && page.has_more);
    for (uint8_t i = 0; i < page.count; ++i) assert(page.rows[i].occurrence.start == today + i * 86400 + 9 * 3600);
    store.records[0] = monthly;
    assert(queryMonth(store, 2024, 2, days) == StoreResult::Ok && days == 0);
    assert(queryMonth(store, 2024, 3, days) == StoreResult::Ok && days == (uint32_t{1} << 30));
    std::printf("AgendaPage=%zu Occurrence=%zu CivilTime=%zu month_mask=%zu\n",
                sizeof(AgendaPage), sizeof(Occurrence), sizeof(CivilTime), sizeof(days));
}

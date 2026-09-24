#include "agenda/domain/recurrence.h"

#include <algorithm>

namespace agenda
{
namespace
{
int64_t daysBeforeYear(uint16_t year)
{
    const int64_t previous = year - 1;
    return previous * 365 + previous / 4 - previous / 100 + previous / 400;
}

bool emit(const EventRecord& event, int64_t start, Occurrence& out)
{
    const bool has_end = (event.flags & HasEndTime) != 0;
    const int64_t duration = has_end ? event.end_time - event.start_time : 0;
    if (start > kLastCalendarSecond - duration) return false;
    out.start = start;
    out.end = start + duration;
    out.event_id = event.id;
    out.has_end = has_end;
    return true;
}
} // namespace

uint8_t daysInMonth(uint16_t year, uint8_t month)
{
    constexpr uint8_t lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    return lengths[month - 1] + (month == 2 && leap ? 1 : 0);
}

bool toCalendarSeconds(const CivilTime& civil, int64_t& seconds)
{
    seconds = 0;
    if (civil.year < 1970 || civil.year > 9999 || civil.month < 1 || civil.month > 12 ||
        civil.day < 1 || civil.day > daysInMonth(civil.year, civil.month) ||
        civil.hour > 23 || civil.minute > 59 || civil.second > 59)
        return false;
    int64_t days = daysBeforeYear(civil.year) - daysBeforeYear(1970) + civil.day - 1;
    for (uint8_t month = 1; month < civil.month; ++month) days += daysInMonth(civil.year, month);
    seconds = days * 86400 + civil.hour * 3600 + civil.minute * 60 + civil.second;
    return true;
}

bool fromCalendarSeconds(int64_t seconds, CivilTime& civil)
{
    civil = {};
    if (seconds < 0 || seconds > kLastCalendarSecond) return false;
    const int64_t absolute_days = seconds / 86400 + daysBeforeYear(1970);
    uint16_t low = 1970;
    uint16_t high = 10000;
    while (high - low > 1)
    {
        const auto middle = static_cast<uint16_t>(low + (high - low) / 2);
        if (daysBeforeYear(middle) <= absolute_days) low = middle;
        else high = middle;
    }
    civil.year = low;
    int64_t day = absolute_days - daysBeforeYear(low);
    while (day >= daysInMonth(low, civil.month)) day -= daysInMonth(low, civil.month++);
    civil.day = static_cast<uint8_t>(day + 1);
    const int64_t within_day = seconds % 86400;
    civil.hour = static_cast<uint8_t>(within_day / 3600);
    civil.minute = static_cast<uint8_t>(within_day % 3600 / 60);
    civil.second = static_cast<uint8_t>(within_day % 60);
    return true;
}

bool nextOccurrence(const EventRecord& event, int64_t not_before, Occurrence& out)
{
    out = {};
    if (event.state != RecordState::Active || !validEvent(event) ||
        event.start_time > kLastCalendarSecond || not_before > kLastCalendarSecond ||
        ((event.flags & HasEndTime) && event.end_time > kLastCalendarSecond))
        return false;
    const int64_t lower = std::max(event.start_time, not_before);
    if (lower == event.start_time) return emit(event, lower, out);
    if (event.repeat == Repeat::None) return false;
    if (event.repeat == Repeat::Daily || event.repeat == Repeat::Weekly)
    {
        const int64_t period = event.repeat == Repeat::Daily ? 86400 : 7 * 86400;
        const int64_t elapsed = lower - event.start_time;
        const int64_t steps = elapsed / period + (elapsed % period != 0);
        return emit(event, event.start_time + steps * period, out);
    }
    CivilTime anchor;
    CivilTime candidate;
    if (!fromCalendarSeconds(event.start_time, anchor) || !fromCalendarSeconds(lower, candidate)) return false;
    candidate.day = anchor.day;
    candidate.hour = anchor.hour;
    candidate.minute = anchor.minute;
    candidate.second = anchor.second;
    if (event.repeat == Repeat::Yearly) candidate.month = anchor.month;
    while (candidate.year <= 9999)
    {
        int64_t start = 0;
        if (toCalendarSeconds(candidate, start) && start >= lower) return emit(event, start, out);
        if (event.repeat == Repeat::Yearly) ++candidate.year;
        else if (++candidate.month > 12)
        {
            candidate.month = 1;
            ++candidate.year;
        }
    }
    return false;
}

} // namespace agenda

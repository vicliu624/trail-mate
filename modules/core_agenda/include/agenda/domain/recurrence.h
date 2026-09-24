#pragma once

#include "agenda/domain/event.h"

namespace agenda
{

// Calendar-local seconds since 1970-01-01, not UTC. Platform clock adapters
// translate the product timezone. Repetition preserves the local time of day.
constexpr int64_t kLastCalendarSecond = 253402300799LL; // 9999-12-31 23:59:59
struct CivilTime
{
    uint16_t year = 1970;
    uint8_t month = 1;
    uint8_t day = 1;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
};

uint8_t daysInMonth(uint16_t year, uint8_t month);
bool toCalendarSeconds(const CivilTime& civil, int64_t& seconds);
bool fromCalendarSeconds(int64_t seconds, CivilTime& civil);

struct Occurrence
{
    int64_t start = 0;
    int64_t end = 0;
    uint32_t event_id = 0;
    bool has_end = false;
};

// Inclusive lower bound. Invalid dates are skipped, never normalized: Jan 31
// monthly -> Mar 31, Feb 29 yearly -> next leap year's Feb 29. No stored copies.
bool nextOccurrence(const EventRecord& event, int64_t not_before, Occurrence& out);

} // namespace agenda

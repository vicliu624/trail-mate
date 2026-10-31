#pragma once
#include "platform/linux/oneseg_service_information.h"
#include <algorithm>
#include <optional>

namespace platform::linux_runtime
{
struct OneSegEventSchedule
{
    enum class State
    {
        Undefined,
        Known,
        Invalid
    };
    State start = State::Undefined, duration = State::Undefined;
    uint16_t start_mjd = 0;
    uint32_t start_jst_seconds = 0, duration_seconds = 0;
    int64_t start_utc_seconds = 0;
    std::optional<int64_t> scheduledEndUtc() const
    {
        if (start != State::Known || duration != State::Known) return std::nullopt;
        return start_utc_seconds + duration_seconds;
    }
};

// ARIB STD-B10 5.2.7 / Annex C: EIT uses MJD + JST BCD, not DVB UTC.
// All-ones fields mean undefined. Scheduled times do not prove that a live
// programme has ended; freshness and running_status remain separate evidence.
inline OneSegEventSchedule oneseg_event_schedule(const OneSegServiceInformation::Event& event)
{
    OneSegEventSchedule result;
    if (!event.valid) return result;
    const auto bcd = [](uint8_t value, unsigned maximum) -> std::optional<unsigned>
    {
        const unsigned high = value >> 4, low = value & 15;
        const unsigned decoded = high * 10 + low;
        if (high > 9 || low > 9 || decoded > maximum) return std::nullopt;
        return decoded;
    };
    if (!std::all_of(event.start.begin(), event.start.end(), [](uint8_t byte)
                     { return byte == 255; }))
    {
        const auto hour = bcd(event.start[2], 23), minute = bcd(event.start[3], 59), second = bcd(event.start[4], 59);
        result.start = OneSegEventSchedule::State::Invalid;
        if (hour && minute && second)
        {
            result.start = OneSegEventSchedule::State::Known;
            result.start_mjd = uint16_t(event.start[0]) * 256 + event.start[1];
            result.start_jst_seconds = *hour * 3600 + *minute * 60 + *second;
            result.start_utc_seconds = (int64_t(result.start_mjd) - 40587) * 86400 + result.start_jst_seconds - 9 * 3600;
        }
    }
    if (!std::all_of(event.duration.begin(), event.duration.end(), [](uint8_t byte)
                     { return byte == 255; }))
    {
        const auto hour = bcd(event.duration[0], 99), minute = bcd(event.duration[1], 59), second = bcd(event.duration[2], 59);
        result.duration = OneSegEventSchedule::State::Invalid;
        if (hour && minute && second)
        {
            result.duration = OneSegEventSchedule::State::Known;
            result.duration_seconds = *hour * 3600 + *minute * 60 + *second;
        }
    }
    return result;
}
} // namespace platform::linux_runtime

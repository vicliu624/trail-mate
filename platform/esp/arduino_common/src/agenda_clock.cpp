#include "platform/esp/arduino_common/agenda_clock.h"

#include "platform/ui/time_runtime.h"

#include <ctime>
#include <esp_timer.h>

namespace platform::esp::arduino_common
{
agenda::ClockSample AgendaClock::sample() const
{
    agenda::ClockSample result;
    const auto utc = ::time(nullptr);
    result.monotonic_seconds = static_cast<uint64_t>(esp_timer_get_time()) / 1000000;
    result.valid = utc >= 1577836800; // Same time-authority threshold as time_runtime.
    if (!result.valid) return result;
    const auto local = platform::ui::time::apply_timezone_offset_for_utc(utc);
    result.calendar_seconds = static_cast<int64_t>(local);
    // The scheduler also detects wall/monotonic divergence (including DST).
    // This identifies explicit profile changes even when offsets coincide.
    result.revision = (static_cast<uint32_t>(platform::ui::time::timezone_profile_id()) << 16) ^
                      static_cast<uint32_t>(platform::ui::time::timezone_offset_min() + 1440);
    return result;
}
} // namespace platform::esp::arduino_common

#include "platform/linux/agenda_clock.h"
#include "agenda/domain/recurrence.h"
#include "platform/ui/time_runtime.h"
#include <chrono>
#include <ctime>
#include <limits>
#include <utility>
namespace platform::linux_runtime
{
namespace
{
uint64_t monotonicSeconds()
{
#if defined(__linux__)
    struct timespec value
    {
    };
    if (::clock_gettime(CLOCK_BOOTTIME, &value) == 0 && value.tv_sec >= 0) return static_cast<uint64_t>(value.tv_sec);
#endif
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
} // namespace
LinuxAgendaClock::LinuxAgendaClock(Sources sources) : sources_(std::move(sources)) {}
::agenda::ClockSample LinuxAgendaClock::sample() const
{
    ::agenda::ClockSample sample;
    sample.monotonic_seconds = sources_.monotonic ? sources_.monotonic() : monotonicSeconds();
    const int64_t utc = sources_.utc ? sources_.utc() : static_cast<int64_t>(std::time(nullptr));
    // Refuse an unrepresentable wall clock instead of narrowing into another
    // date on a platform whose time_t has a smaller range than the core codec.
    if (utc < 1577836800 || utc > static_cast<int64_t>(std::numeric_limits<std::time_t>::max())) return sample;
    const auto local = ::platform::ui::time::apply_timezone_offset_for_utc(static_cast<std::time_t>(utc));
    if (local < 0 || static_cast<int64_t>(local) > ::agenda::kLastCalendarSecond) return sample;
    sample.calendar_seconds = static_cast<int64_t>(local);
    const auto offset = (sample.calendar_seconds - utc) / 60;
    sample.revision = (static_cast<uint32_t>(::platform::ui::time::timezone_profile_id()) << 16) ^ static_cast<uint32_t>(offset + 1440);
    sample.valid = true;
    return sample;
}
} // namespace platform::linux_runtime

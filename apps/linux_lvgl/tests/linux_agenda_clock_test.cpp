#include "agenda/domain/recurrence.h"
#include "platform/linux/agenda_clock.h"
#include "platform/ui/settings_store.h"
#include "platform/ui/time_runtime.h"
#include "platform/ui/timezone_profile.h"
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <future>
#include <limits>
#include <unistd.h>

int64_t date(unsigned year, unsigned month, unsigned day, unsigned hour, unsigned minute, unsigned second)
{
    int64_t result = 0;
    assert(agenda::toCalendarSeconds({uint16_t(year), uint8_t(month), uint8_t(day), uint8_t(hour), uint8_t(minute), uint8_t(second)}, result));
    return result;
}
int main()
{
    char root[] = "/tmp/trailmate-agenda-clock-XXXXXX";
    assert(mkdtemp(root));
    setenv("TRAIL_MATE_SETTINGS_ROOT", root, 1);
    unsetenv("TRAIL_MATE_TZ_OFFSET_MIN");
    setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0/2", 1);
    tzset();
    int64_t utc = date(2026, 3, 8, 6, 59, 59);
    uint64_t monotonic = 100;
    platform::linux_runtime::LinuxAgendaClock clock({[&]
                                                     { return utc; },
                                                     [&]
                                                     { return monotonic; }});
    const auto before = clock.sample();
    assert(before.valid && before.calendar_seconds == date(2026, 3, 8, 1, 59, 59) && before.monotonic_seconds == 100);
    ++utc;
    ++monotonic;
    const auto after = clock.sample();
    assert(after.valid && after.calendar_seconds == date(2026, 3, 8, 3, 0, 0) && before.revision != after.revision);
    utc = date(2026, 11, 1, 5, 59, 59);
    assert(clock.sample().calendar_seconds == date(2026, 11, 1, 1, 59, 59));
    ++utc;
    assert(clock.sample().calendar_seconds == date(2026, 11, 1, 1, 0, 0));
    setenv("TZ", "IST-5:30", 1);
    assert(clock.sample().calendar_seconds == utc + 330 * 60);
    setenv("TRAIL_MATE_TZ_OFFSET_MIN", "480", 1);
    const auto fixed = clock.sample();
    assert(fixed.calendar_seconds == utc + 480 * 60);
    platform::ui::time::set_timezone_profile_id(2); // Taipei: same offset, distinct profile.
    const auto changed = clock.sample();
    assert(changed.calendar_seconds == fixed.calendar_seconds && changed.revision != fixed.revision);
    utc = -1;
    assert(!clock.sample().valid && clock.sample().monotonic_seconds == monotonic);
    platform::ui::settings_store::put_int("settings", "timezone_profile", platform::ui::time::kFixedTimezoneProfileId);
    setenv("TRAIL_MATE_TZ_OFFSET_MIN", "60", 1);
    utc = std::numeric_limits<std::time_t>::max();
    assert(platform::ui::time::apply_timezone_offset(static_cast<std::time_t>(utc)) == static_cast<std::time_t>(-1));
    assert(!clock.sample().valid);
    if (sizeof(std::time_t) < sizeof(int64_t))
    {
        ++utc;
        assert(!clock.sample().valid);
    }
    const char* keys[] = {"timezone_profile", "timezone_offset"};
    platform::ui::settings_store::remove_keys("settings", keys, 2);
    unsetenv("TRAIL_MATE_TZ_OFFSET_MIN");
    setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0/2", 1);
    tzset();
    const auto run = [](int64_t utc, int offset)
    {
        platform::linux_runtime::LinuxAgendaClock clock({[utc]
                                                         { return utc; },
                                                         []
                                                         { return uint64_t{10}; }});
        for (unsigned i = 0; i < 100; ++i) assert(clock.sample().calendar_seconds == utc + offset * 60);
    };
    auto winter = std::async(std::launch::async, run, date(2026, 1, 1, 0, 0, 0), -300);
    auto summer = std::async(std::launch::async, run, date(2026, 7, 1, 0, 0, 0), -240);
    winter.get();
    summer.get();
    platform::linux_runtime::LinuxAgendaClock system;
    assert(system.sample().valid);
    struct timespec boottime
    {
    };
    assert(clock_gettime(CLOCK_BOOTTIME, &boottime) == 0);
    const auto native = system.sample().monotonic_seconds;
    assert(native >= static_cast<uint64_t>(boottime.tv_sec) && native <= static_cast<uint64_t>(boottime.tv_sec) + 1);
    std::filesystem::remove_all(root);
}

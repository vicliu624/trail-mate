#include "platform/ui/settings_store.h"
#include "platform/ui/time_runtime.h"
#include <cassert>
#include <cerrno>
#include <limits>

namespace
{
unsigned calls = 0;
int failure = 0;
clockid_t destination;
timespec observed{};
} // namespace
extern "C" int clock_settime(clockid_t clock, const struct timespec* value) noexcept
{
    ++calls;
    destination = clock;
    observed = *value;
    errno = failure;
    return failure ? -1 : 0;
}
// Link unrelated timezone functions without touching persistent device settings.
namespace platform::ui::settings_store
{
int get_int(const char*, const char*, int fallback) { return fallback; }
void put_int(const char*, const char*, int) { assert(false); }
} // namespace platform::ui::settings_store
int main()
{
    using platform::ui::time::set_utc_time;
    assert(!set_utc_time(-1) && calls == 0);
    assert(set_utc_time(1700000000));
    assert(calls == 1 && destination == CLOCK_REALTIME && observed.tv_sec == 1700000000 && observed.tv_nsec == 0);
    failure = EPERM;
    assert(!set_utc_time(1700000001) && calls == 2);
    failure = EINVAL;
    assert(!set_utc_time(std::numeric_limits<time_t>::max()) && calls == 3);
    assert(observed.tv_sec == std::numeric_limits<time_t>::max());
    // No real clock write occurs: the process-local syscall symbol is interposed.
}

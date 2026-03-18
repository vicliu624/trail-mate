#include "platform/ui/time_runtime.h"
#include "platform/ui/settings_store.h"
#include "sys/clock.h"

#include <ctime>

namespace platform::ui::time
{
namespace
{

constexpr const char* kSettingsNs = "settings";
constexpr const char* kTimezoneKey = "timezone_offset";

int& timezoneOffsetStorage()
{
    static int s_offset_min = ::platform::ui::settings_store::get_int(kSettingsNs, kTimezoneKey, 0);
    return s_offset_min;
}

} // namespace

int timezone_offset_min()
{
    return timezoneOffsetStorage();
}

void set_timezone_offset_min(int offset_min)
{
    timezoneOffsetStorage() = offset_min;
    ::platform::ui::settings_store::put_int(kSettingsNs, kTimezoneKey, offset_min);
}

time_t apply_timezone_offset(time_t utc_seconds)
{
    if (utc_seconds <= 0)
    {
        return utc_seconds;
    }
    return utc_seconds + static_cast<time_t>(timezone_offset_min()) * 60;
}

bool localtime_now(struct tm* out_tm)
{
    if (!out_tm)
    {
        return false;
    }
    const time_t now = apply_timezone_offset(static_cast<time_t>(sys::epoch_seconds_now()));
    const tm* tmp = gmtime(&now);
    if (!tmp)
    {
        return false;
    }
    *out_tm = *tmp;
    return true;
}

} // namespace platform::ui::time

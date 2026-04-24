#include "platform/ui/time_runtime.h"

#include "platform/ui/settings_store.h"

#include <cstdlib>
#include <ctime>

namespace platform::ui::time
{
namespace
{

constexpr const char* kSettingsNs = "settings";
constexpr const char* kTimezoneOffsetKey = "timezone_offset";
constexpr const char* kTimezoneOffsetEnv = "TRAIL_MATE_TZ_OFFSET_MIN";

int env_timezone_offset_or_default(int fallback)
{
    const char* value = std::getenv(kTimezoneOffsetEnv);
    if (!value || value[0] == '\0')
    {
        return fallback;
    }

    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || (end && *end != '\0'))
    {
        return fallback;
    }
    return static_cast<int>(parsed);
}

} // namespace

int timezone_offset_min()
{
    return env_timezone_offset_or_default(
        ::platform::ui::settings_store::get_int(kSettingsNs, kTimezoneOffsetKey, 0));
}

void set_timezone_offset_min(int offset_min)
{
    ::platform::ui::settings_store::put_int(kSettingsNs, kTimezoneOffsetKey, offset_min);
}

::time_t apply_timezone_offset(::time_t utc_seconds)
{
    return utc_seconds + static_cast<time_t>(timezone_offset_min()) * 60;
}

bool localtime_now(struct tm* out_tm)
{
    if (!out_tm)
    {
        return false;
    }

    const ::time_t now = ::time(nullptr);
    if (now <= 0)
    {
        return false;
    }

    const ::time_t local = apply_timezone_offset(now);
    const ::tm* tmp = ::gmtime(&local);
    if (!tmp)
    {
        return false;
    }

    *out_tm = *tmp;
    return true;
}

} // namespace platform::ui::time

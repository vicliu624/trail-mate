#include "platform/ui/time_runtime.h"

#include "ui/ui_common.h"

namespace platform::ui::time
{

int timezone_offset_min()
{
    return ui_get_timezone_offset_min();
}

void set_timezone_offset_min(int offset_min)
{
    ui_set_timezone_offset_min(offset_min);
}

time_t apply_timezone_offset(time_t utc_seconds)
{
    return ui_apply_timezone_offset(utc_seconds);
}

bool localtime_now(struct tm* out_tm)
{
    if (!out_tm)
    {
        return false;
    }
    const time_t local = apply_timezone_offset(time(nullptr));
    const tm* tmp = gmtime(&local);
    if (!tmp)
    {
        return false;
    }
    *out_tm = *tmp;
    return true;
}

} // namespace platform::ui::time

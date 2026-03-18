#pragma once

#include <cstdint>
#include <ctime>

namespace platform::ui::time
{

int timezone_offset_min();
void set_timezone_offset_min(int offset_min);
time_t apply_timezone_offset(time_t utc_seconds);
bool localtime_now(struct tm* out_tm);

} // namespace platform::ui::time

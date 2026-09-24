#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace ui::agenda
{
// T is this occurrence's start, not its reminder deadline. Compact SI-style
// units avoid allocating translated duration strings on each timer tick.
inline void formatCountdown(int64_t start, int64_t now, bool valid, char* out, std::size_t size)
{
    if (!valid)
    {
        std::snprintf(out, size, "--");
        return;
    }
    const bool future = start > now;
    const uint64_t seconds = future ? uint64_t(start) - uint64_t(now) : uint64_t(now) - uint64_t(start);
    const uint64_t minutes = seconds / 60 + (future && seconds % 60 ? 1 : 0);
    const char sign = future ? '-' : '+';
    if (minutes >= 1440)
        std::snprintf(out, size, "T%c%llud%lluh", sign, (unsigned long long)(minutes / 1440), (unsigned long long)(minutes / 60 % 24));
    else if (minutes >= 60)
        std::snprintf(out, size, "T%c%lluh%llum", sign, (unsigned long long)(minutes / 60), (unsigned long long)(minutes % 60));
    else
        std::snprintf(out, size, "T%c%llum", sign, (unsigned long long)minutes);
}
} // namespace ui::agenda

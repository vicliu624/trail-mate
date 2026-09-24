#pragma once
#include <cstdint>
// Deterministic hardware-call timing for the production driver's host tests.
inline int64_t esp_timer_get_time()
{
    static int64_t ticks = 0;
    ticks += 10;
    return ticks;
}

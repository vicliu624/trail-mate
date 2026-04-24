#include "sys/clock.h"

#include <chrono>
#include <ctime>

namespace sys
{
namespace
{
MillisProvider g_millis_provider = nullptr;
EpochSecondsProvider g_epoch_provider = nullptr;
SleepProvider g_sleep_provider = nullptr;

uint32_t fallback_millis_now()
{
    using clock = std::chrono::steady_clock;
    static const auto start = clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - start).count();
    return static_cast<uint32_t>(elapsed);
}

uint32_t fallback_epoch_seconds_now()
{
    const std::time_t now = std::time(nullptr);
    return now < 0 ? 0U : static_cast<uint32_t>(now);
}

void fallback_sleep_ms(uint32_t ms)
{
    const uint32_t start = fallback_millis_now();
    while ((fallback_millis_now() - start) < ms)
    {
    }
}

} // namespace

void set_millis_provider(MillisProvider provider)
{
    g_millis_provider = provider;
}

void set_epoch_seconds_provider(EpochSecondsProvider provider)
{
    g_epoch_provider = provider;
}

void set_sleep_provider(SleepProvider provider)
{
    g_sleep_provider = provider;
}

uint32_t millis_now()
{
    return g_millis_provider ? g_millis_provider() : fallback_millis_now();
}

uint32_t uptime_seconds_now()
{
    return millis_now() / 1000U;
}

uint32_t epoch_seconds_now()
{
    return g_epoch_provider ? g_epoch_provider() : fallback_epoch_seconds_now();
}

void sleep_ms(uint32_t ms)
{
    if (g_sleep_provider)
    {
        g_sleep_provider(ms);
        return;
    }

    fallback_sleep_ms(ms);
}

} // namespace sys

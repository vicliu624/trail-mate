#pragma once

#include <cstdint>

namespace sys
{

using MillisProvider = uint32_t (*)();
using EpochSecondsProvider = uint32_t (*)();
using SleepProvider = void (*)(uint32_t);

void set_millis_provider(MillisProvider provider);
void set_epoch_seconds_provider(EpochSecondsProvider provider);
void set_sleep_provider(SleepProvider provider);

uint32_t millis_now();
uint32_t uptime_seconds_now();
uint32_t epoch_seconds_now();
void sleep_ms(uint32_t ms);

} // namespace sys

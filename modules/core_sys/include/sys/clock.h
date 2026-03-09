#pragma once

#include <cstdint>

namespace sys
{

using MillisProvider = uint32_t (*)();
using EpochSecondsProvider = uint32_t (*)();

void set_millis_provider(MillisProvider provider);
void set_epoch_seconds_provider(EpochSecondsProvider provider);

uint32_t millis_now();
uint32_t uptime_seconds_now();
uint32_t epoch_seconds_now();

} // namespace sys

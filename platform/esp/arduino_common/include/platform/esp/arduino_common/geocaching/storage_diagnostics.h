#pragma once
#include <cstdint>
#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace platform::esp::arduino_common::geocaching
{
// Terminal read failures only. Keep the reader's location before its file and
// borrowed views are released; successful work and Busy never reach this hook.
inline void reportIndexReadFailure(const char* reader, unsigned phase, unsigned result, unsigned table, uint64_t offset)
{
#if defined(ARDUINO)
    Serial.printf("[Geocaching][IndexFail] reader=%s phase=%u result=%u table=%u offset=%llu\n",
                  reader, phase, result, table, static_cast<unsigned long long>(offset));
#else
    (void)reader;
    (void)phase;
    (void)result;
    (void)table;
    (void)offset;
#endif
}
} // namespace platform::esp::arduino_common::geocaching

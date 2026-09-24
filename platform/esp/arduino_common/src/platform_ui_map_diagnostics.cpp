#include "platform/ui/map_diagnostics.h"

#if defined(TRAIL_MATE_MAP_DIAGNOSTICS) && TRAIL_MATE_MAP_DIAGNOSTICS
#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace platform::ui
{
void mapDiagnosticLog(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    va_list measure;
    va_copy(measure, args);
    const int size = std::vsnprintf(nullptr, 0, format, measure);
    va_end(measure);
    if (size > 0)
    {
        // Diagnostic-only transient storage, not a large ESP task-stack buffer.
        auto* text = static_cast<char*>(std::malloc(static_cast<std::size_t>(size) + 1));
        if (text)
        {
            std::vsnprintf(text, static_cast<std::size_t>(size) + 1, format, args);
            Serial.write(reinterpret_cast<const uint8_t*>(text), static_cast<std::size_t>(size));
            std::free(text);
        }
    }
    va_end(args);
}
} // namespace platform::ui
#endif

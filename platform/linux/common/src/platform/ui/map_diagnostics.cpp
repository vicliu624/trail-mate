#include "platform/ui/map_diagnostics.h"

#if defined(TRAIL_MATE_MAP_DIAGNOSTICS) && TRAIL_MATE_MAP_DIAGNOSTICS
#include <cstdarg>
#include <cstdio>

namespace platform::ui
{
void mapDiagnosticLog(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
}
} // namespace platform::ui
#endif

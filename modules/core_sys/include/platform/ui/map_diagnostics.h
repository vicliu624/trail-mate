#pragma once

namespace platform::ui
{
// Optional diagnostic output. Callers must not hold filesystem or queue locks.
void mapDiagnosticLog(const char* format, ...);
} // namespace platform::ui

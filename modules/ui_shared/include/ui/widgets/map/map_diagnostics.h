#pragma once

// Shared UI knows only the diagnostic contract; platforms select the output.
#ifndef TRAIL_MATE_MAP_DIAGNOSTICS
#define TRAIL_MATE_MAP_DIAGNOSTICS 0
#endif

#if TRAIL_MATE_MAP_DIAGNOSTICS
#include "platform/ui/map_diagnostics.h"
#define MAP_DIAG(...) ::platform::ui::mapDiagnosticLog(__VA_ARGS__)
#else
#define MAP_DIAG(...) ((void)0)
#endif

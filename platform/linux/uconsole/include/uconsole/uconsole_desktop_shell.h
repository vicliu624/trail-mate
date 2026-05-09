#pragma once

#include "platform/surface_presenter.h"

namespace trailmate::uconsole
{

struct UConsoleShellOptions
{
    int width = 1280;
    int height = 720;
    int frame_time_ms = 16;
};

void runUConsoleShell(::trailmate::cardputer_zero::platform::SurfacePresenter& presenter,
                      UConsoleShellOptions options = {});

} // namespace trailmate::uconsole

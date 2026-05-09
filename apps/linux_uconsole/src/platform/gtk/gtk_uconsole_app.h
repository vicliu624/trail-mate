#pragma once

#include <string>

namespace trailmate::uconsole::gtk
{

struct GtkUConsoleOptions
{
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
    std::string title = "Trail Mate uConsole";
};

int runGtkUConsoleApp(GtkUConsoleOptions options = {});

} // namespace trailmate::uconsole::gtk

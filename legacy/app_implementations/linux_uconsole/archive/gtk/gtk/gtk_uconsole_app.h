#pragma once

#include <string>

namespace trailmate::uconsole::gtk
{

struct GtkUConsoleOptions
{
    int width = 1180;
    int height = 600;
    bool fullscreen = false;
    std::string title = "Trail Mate uConsole";
};

int runGtkUConsoleApp(GtkUConsoleOptions options = {});

} // namespace trailmate::uconsole::gtk

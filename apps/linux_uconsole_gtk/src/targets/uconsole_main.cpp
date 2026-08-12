#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

#include "platform/gtk/gtk_uconsole_app.h"
#include "uconsole/uconsole_hardware_probe.h"

namespace
{

int envInt(const char* name, int fallback)
{
    if (const char* value = std::getenv(name))
    {
        if (*value != '\0')
        {
            try
            {
                return std::stoi(value);
            }
            catch (...)
            {
                return fallback;
            }
        }
    }
    return fallback;
}

struct LaunchOptions
{
    int width = 1280;
    int height = 720;
    bool fullscreen = true;
};

LaunchOptions parseOptions(int argc, char** argv)
{
    LaunchOptions options{};
    options.width = envInt("TRAIL_MATE_UCONSOLE_WIDTH", 1280);
    options.height = envInt("TRAIL_MATE_UCONSOLE_HEIGHT", 720);
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view current{argv[index]};
        if (current == "--width" && (index + 1) < argc)
        {
            options.width = std::stoi(argv[++index]);
        }
        else if (current == "--height" && (index + 1) < argc)
        {
            options.height = std::stoi(argv[++index]);
        }
        else if (current == "--fullscreen")
        {
            options.fullscreen = true;
        }
        else if (current == "--windowed")
        {
            options.fullscreen = false;
        }
    }

    return options;
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const LaunchOptions options = parseOptions(argc, argv);
        trailmate::uconsole::UConsoleHardwareRuntime hardware{};
        if (!hardware.initialize())
        {
            std::cerr << "uConsole hardware setup failed: "
                      << hardware.lastError() << '\n';
        }

        return trailmate::uconsole::gtk::runGtkUConsoleApp(
            {.width = options.width,
             .height = options.height,
             .fullscreen = options.fullscreen,
             .title = "Trail Mate uConsole"});
    }
    catch (const std::exception& ex)
    {
        std::cerr << "uConsole startup failed: " << ex.what() << '\n';
        return 1;
    }
}

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

#include "platform/desktop/sdl_window_presenter.h"
#include "platform/device/linux_framebuffer_platform.h"
#include "platform/gtk/gtk_uconsole_app.h"
#include "uconsole/uconsole_desktop_shell.h"

namespace
{

std::string envString(const char* name, const char* fallback)
{
    if (const char* value = std::getenv(name))
    {
        if (*value != '\0') return value;
    }
    return fallback;
}

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

enum class Backend
{
    GtkWindow,
    SdlWindow,
    Framebuffer,
};

struct LaunchOptions
{
    Backend backend = Backend::GtkWindow;
    std::string framebuffer = "/dev/fb0";
    trailmate::uconsole::UConsoleShellOptions shell{};
    int window_scale = 1;
    bool fullscreen = false;
};

LaunchOptions parseOptions(int argc, char** argv)
{
    LaunchOptions options{};
    options.framebuffer = envString("TRAIL_MATE_FBDEV", "/dev/fb0");

    for (int index = 1; index < argc; ++index)
    {
        const std::string_view current{argv[index]};
        if (current == "--fbdev")
        {
            options.backend = Backend::Framebuffer;
            if ((index + 1) < argc)
            {
                options.framebuffer = argv[++index];
            }
        }
        else if (current == "--sdl")
        {
            options.backend = Backend::SdlWindow;
        }
    }

    const int default_width =
        options.backend == Backend::Framebuffer ? 720 : 1280;
    const int default_height =
        options.backend == Backend::Framebuffer ? 1280 : 720;
    options.shell.width =
        envInt("TRAIL_MATE_UCONSOLE_WIDTH", default_width);
    options.shell.height =
        envInt("TRAIL_MATE_UCONSOLE_HEIGHT", default_height);
    options.window_scale = envInt("TRAIL_MATE_UCONSOLE_WINDOW_SCALE", 1);

    for (int index = 1; index < argc; ++index)
    {
        const std::string_view current{argv[index]};
        if (current == "--fbdev" && (index + 1) < argc)
        {
            options.framebuffer = argv[++index];
        }
        else if (current == "--sdl")
        {
            options.backend = Backend::SdlWindow;
        }
        else if (current == "--width" && (index + 1) < argc)
        {
            options.shell.width = std::stoi(argv[++index]);
        }
        else if (current == "--height" && (index + 1) < argc)
        {
            options.shell.height = std::stoi(argv[++index]);
        }
        else if (current == "--window-scale" && (index + 1) < argc)
        {
            options.window_scale = std::stoi(argv[++index]);
        }
        else if (current == "--fullscreen")
        {
            options.fullscreen = true;
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
        if (options.backend == Backend::Framebuffer)
        {
            trailmate::cardputer_zero::platform::device::
                LinuxFramebufferPlatform device{options.framebuffer};
            trailmate::uconsole::runUConsoleShell(device, options.shell);
        }
        else if (options.backend == Backend::GtkWindow)
        {
            return trailmate::uconsole::gtk::runGtkUConsoleApp(
                {.width = options.shell.width,
                 .height = options.shell.height,
                 .fullscreen = options.fullscreen,
                 .title = "Trail Mate uConsole"});
        }
        else
        {
            trailmate::uconsole::desktop::SdlWindowPresenter window{
                {.width = options.shell.width,
                 .height = options.shell.height,
                 .scale = options.window_scale,
                 .fullscreen = options.fullscreen,
                 .title = "Trail Mate uConsole"}};
            trailmate::uconsole::runUConsoleShell(window, options.shell);
        }
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "uConsole startup failed: " << ex.what() << '\n';
        return 1;
    }
}

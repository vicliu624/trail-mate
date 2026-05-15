#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

#include "platform/gtk/gtk_uconsole_app.h"
#if defined(TRAIL_MATE_UCONSOLE_HAS_SDL)
#include "platform/desktop/sdl_window_presenter.h"
#endif
#if defined(TRAIL_MATE_UCONSOLE_HAS_LEGACY_SURFACE)
#include "platform/device/linux_framebuffer_platform.h"
#include "uconsole/uconsole_desktop_shell.h"
#endif

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
    int width = 1180;
    int height = 600;
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
        options.backend == Backend::Framebuffer ? 1280
        : options.backend == Backend::GtkWindow ? 1180
                                                : 1280;
    const int default_height =
        options.backend == Backend::Framebuffer ? 720
        : options.backend == Backend::GtkWindow ? 600
                                                : 720;
    options.width = envInt("TRAIL_MATE_UCONSOLE_WIDTH", default_width);
    options.height = envInt("TRAIL_MATE_UCONSOLE_HEIGHT", default_height);
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
            options.width = std::stoi(argv[++index]);
        }
        else if (current == "--height" && (index + 1) < argc)
        {
            options.height = std::stoi(argv[++index]);
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
#if defined(TRAIL_MATE_UCONSOLE_HAS_LEGACY_SURFACE)
            trailmate::uconsole::UConsoleShellOptions shell{};
            shell.width = options.width;
            shell.height = options.height;
            trailmate::cardputer_zero::platform::device::
                LinuxFramebufferPlatform device{options.framebuffer};
            trailmate::uconsole::runUConsoleShell(device, shell);
#else
            throw std::runtime_error(
                "framebuffer backend is not compiled into this package; run trailmate-uconsole for the GTK UI");
#endif
        }
        else if (options.backend == Backend::GtkWindow)
        {
            return trailmate::uconsole::gtk::runGtkUConsoleApp(
                {.width = options.width,
                 .height = options.height,
                 .fullscreen = options.fullscreen,
                 .title = "Trail Mate uConsole"});
        }
        else
        {
#if defined(TRAIL_MATE_UCONSOLE_HAS_SDL)
            trailmate::uconsole::desktop::SdlWindowPresenter window{
                {.width = options.width,
                 .height = options.height,
                 .scale = options.window_scale,
                 .fullscreen = options.fullscreen,
                 .title = "Trail Mate uConsole"}};
            trailmate::uconsole::UConsoleShellOptions shell{};
            shell.width = options.width;
            shell.height = options.height;
            trailmate::uconsole::runUConsoleShell(window, shell);
#else
            throw std::runtime_error(
                "SDL backend is not compiled into this package; run trailmate-uconsole for the GTK UI");
#endif
        }
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "uConsole startup failed: " << ex.what() << '\n';
        return 1;
    }
}

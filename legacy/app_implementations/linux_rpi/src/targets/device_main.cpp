#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

#include "platform/device/linux_framebuffer_platform.h"
#include "ui/shell_ui_runner.h"

namespace
{

std::string parseFramebufferPath(int argc, char** argv)
{
    std::string path = "/dev/fb0";

    if (const char* env = std::getenv("TRAIL_MATE_FBDEV"))
    {
        if (*env != '\0')
        {
            path = env;
        }
    }

    for (int index = 1; index < argc; ++index)
    {
        const std::string_view current{argv[index]};
        if (current == "--fbdev" && (index + 1) < argc)
        {
            path = argv[index + 1];
            ++index;
        }
    }

    return path;
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        trailmate::cardputer_zero::platform::device::LinuxFramebufferPlatform device{parseFramebufferPath(argc, argv)};
        trailmate::cardputer_zero::linux_ui::runShellUi(device);
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Device startup failed: " << ex.what() << '\n';
        return 1;
    }
}

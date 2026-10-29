#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

#include "platform/device/linux_framebuffer_platform.h"
#include "platform/linux/application_lifecycle.h"
#include "ui/shell_ui_runner.h"

namespace
{

std::string framebuffer_path()
{
    if (const char* env = std::getenv("TRAIL_MATE_FRAMEBUFFER"))
    {
        if (env[0] != '\0')
        {
            return env;
        }
    }
    return "/dev/fb0";
}

std::chrono::milliseconds auto_exit_after()
{
    if (const char* env = std::getenv("TRAIL_MATE_AUTO_EXIT_MS"))
    {
        if (env[0] != '\0')
        {
            return std::chrono::milliseconds(std::strtoul(env, nullptr, 10));
        }
    }
    return std::chrono::milliseconds::zero();
}

} // namespace

int main(int, char** argv)
{
    return ::platform::linux_runtime::run_application(argv, [&]() -> int
                                                      {
    try
    {
        trailmate::cardputer_zero::platform::device::LinuxFramebufferPlatform
            presenter(framebuffer_path());
        trailmate::cardputer_zero::linux_ui::runShellUi(
            presenter,
            auto_exit_after(), true);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "trailmate-cardputer-zero: %s\n", error.what());
        return 1;
    } });
}

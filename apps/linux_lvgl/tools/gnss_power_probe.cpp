#include "platform/desktop/device_options.h"
#include "platform/linux/gpio_power.h"
#include <chrono>
#include <cstdio>
#include <exception>
#include <thread>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "Usage: %s DEVICE_PROFILE\n", argv[0]);
        return 2;
    }
    try
    {
        const auto options = platform::desktop::loadDeviceOptions(argv[1]).gnss.power;
        if (options.line < 0)
        {
            std::fprintf(stderr, "Profile has no GNSS power line\n");
            return 2;
        }
        platform::linux_runtime::AsyncGpioPower power(options);
        using State = platform::linux_runtime::AsyncGpioPower::State;
        using Clock = std::chrono::steady_clock;
        power.request(true);
        const auto deadline = Clock::now() + std::chrono::milliseconds(options.startup_ms) + std::chrono::seconds(3);
        auto state = power.snapshot();
        while (Clock::now() < deadline && state.state != State::Ready && state.state != State::Failed)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            state = power.snapshot();
        }
        const bool ready = state.state == State::Ready;
        std::printf("gnss_power_ready=%d error=%d\n", ready, state.error);
        std::fflush(stdout);
        if (ready) std::this_thread::sleep_for(std::chrono::seconds(2));
        power.request(false);
        const auto off_deadline = Clock::now() + std::chrono::seconds(3);
        do
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            state = power.snapshot();
        } while (Clock::now() < off_deadline && state.state == State::Stopping);
        const bool off = state.state == State::Off;
        std::printf("gnss_power_released=%d error=%d\n", off, state.error);
        return ready && off ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}

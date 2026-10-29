#include "platform/linux/backlight.h"
#include "platform/linux/gamma_control.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char** argv)
{
    if (argc != 4 && argc != 5) return 2;
    if (argc == 5)
    {
        using namespace platform::linux_runtime;
        BacklightOptions options;
        options.backend = "wayland_gamma";
        options.gamma = {argv[1], argv[2]};
        BacklightRuntime runtime(options);
        const auto wait = [&](const auto& condition)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
            while (!condition(runtime.snapshot()))
            {
                if (std::chrono::steady_clock::now() >= deadline) return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return true;
        };
        if (!wait([](const auto& snapshot)
                  { return snapshot.available; })) return 3;
        if (!runtime.request(static_cast<uint8_t>(std::stoi(argv[3])))) return 4;
        if (!wait([&](const auto& snapshot)
                  { return snapshot.available && !snapshot.failed && snapshot.level == std::stoi(argv[3]); })) return 5;
        std::cout << "status=0\napplied=1\nhealthy=1" << std::endl;
        return 0;
    }
    std::atomic_bool cancel{false};
    platform::linux_runtime::GammaControl control;
    const auto status = control.connect({argv[1], argv[2]}, cancel);
    std::cout << "status=" << static_cast<int>(status) << std::endl;
    if (status != platform::linux_runtime::GammaControlStatus::Ready) return 0;
    const bool applied = control.apply(static_cast<uint8_t>(std::stoi(argv[3])), cancel);
    std::cout << "applied=" << applied << std::endl;
    if (applied) std::cout << "healthy=" << control.healthy(cancel) << std::endl;
    control.close();
    return 0;
}

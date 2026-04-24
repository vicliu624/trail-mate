#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

#include "app/demo_app_runner.h"
#include "platform/simulator/sdl_simulator.h"

namespace {

struct SimulatorOptions {
    int scale{1};
    int auto_exit_ms{0};
};

SimulatorOptions parseOptions(int argc, char** argv)
{
    SimulatorOptions options{};

    if (const char* env = std::getenv("TRAIL_MATE_SIM_SCALE")) {
        options.scale = std::max(1, std::atoi(env));
    }
    if (const char* env = std::getenv("TRAIL_MATE_SIM_AUTO_EXIT_MS")) {
        options.auto_exit_ms = std::max(0, std::atoi(env));
    }

    for (int index = 1; index < argc; ++index) {
        const std::string_view current{argv[index]};
        if (current == "--scale" && (index + 1) < argc) {
            options.scale = std::max(1, std::atoi(argv[index + 1]));
            ++index;
            continue;
        }

        if (current == "--auto-exit-ms" && (index + 1) < argc) {
            options.auto_exit_ms = std::max(0, std::atoi(argv[index + 1]));
            ++index;
        }
    }

    return options;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const auto options = parseOptions(argc, argv);
        trailmate::cardputer_zero::platform::simulator::SdlSimulator simulator{options.scale};
        trailmate::cardputer_zero::app::runDemoApp(simulator, std::chrono::milliseconds{options.auto_exit_ms});
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Simulator startup failed: " << ex.what() << '\n';
        return 1;
    }
}

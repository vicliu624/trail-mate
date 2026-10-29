#include "platform/ui/hostlink_runtime.h"
#include "app/linux_app_services.h"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace link_runtime = platform::ui::hostlink;
template <class Predicate>
static bool wait_for(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!predicate() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return predicate();
}
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-hostlink-" + std::to_string(getpid()));
    setenv("TRAIL_MATE_SD_ROOT", root.c_str(), 1);
    const auto failed = fork();
    assert(failed >= 0);
    if (!failed)
    {
        setenv("TRAIL_MATE_HOSTLINK_BIND", "not-an-address", 1);
        link_runtime::start();
        assert(wait_for([]
                        { return !link_runtime::is_active(); }));
        assert(link_runtime::get_status().state == link_runtime::LinkState::Error);
        link_runtime::stop();
        std::exit(0); // Exercise static destruction after failed worker + stop.
    }
    int status = 0;
    assert(waitpid(failed, &status, 0) == failed && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    setenv("TRAIL_MATE_HOSTLINK_BIND", "127.0.0.1", 1);
    const auto endpoint = root / "hostlink/endpoint.txt";
    for (unsigned cycle = 0; cycle < 5; ++cycle)
    {
        link_runtime::start();
        assert(wait_for([&]
                        { return std::filesystem::exists(endpoint); }));
        link_runtime::stop();
        assert(!link_runtime::is_active());
        assert(link_runtime::get_status().state == link_runtime::LinkState::Stopped);
        assert(!std::filesystem::exists(endpoint));
    }
    std::vector<std::thread> controllers;
    for (unsigned actor = 0; actor < 4; ++actor)
        controllers.emplace_back([]
                                 {
            for (unsigned cycle = 0; cycle < 10; ++cycle)
            {
                link_runtime::start();
                link_runtime::stop();
            } });
    for (auto& controller : controllers) controller.join();
    link_runtime::stop();
    assert(!link_runtime::is_active() && !std::filesystem::exists(endpoint));
    const auto active = fork();
    assert(active >= 0);
    if (!active)
    {
        link_runtime::start();
        assert(wait_for([&]
                        { return std::filesystem::exists(endpoint); }));
        std::exit(0); // Worker ownership must also handle ordinary process exit.
    }
    assert(waitpid(active, &status, 0) == active && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(!std::filesystem::exists(endpoint));
    {
        trailmate::linux_app::LinuxAppServices services;
        link_runtime::start();
        assert(wait_for([&] { return std::filesystem::exists(endpoint); }));
        services.shutdown();
        assert(!link_runtime::is_active() && !std::filesystem::exists(endpoint));
    }
    std::filesystem::remove_all(root);
}

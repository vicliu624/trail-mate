#include "platform/ui/hostlink_runtime.h"
#include "signed_route_package_fixture.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

int main(int argc, char** argv)
{
    if (argc != 2 && argc != 3) return 1;
    const auto root = std::filesystem::path(argv[1]);
    const auto settings = root / "settings", sd = root / "sd";
    setenv("TRAIL_MATE_SETTINGS_ROOT", settings.c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", sd.c_str(), 1);
    setenv("TRAIL_MATE_HOSTLINK_BIND", "127.0.0.1", 1);
    if (argc == 3)
    {
        const auto fixture = package_fixture::make(argv[2]);
        std::filesystem::create_directories(root);
        const auto write = [&](const char* name, const package_fixture::Bytes& bytes)
        { std::ofstream output(root / name, std::ios::binary | std::ios::trunc); assert(output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size())); };
        write("package.cmp", fixture.signed_manifest);
        write("package.kml", fixture.kml);
        write("package-index.cmp", fixture.index);
        write("package-photo.jpg", fixture.image);
    }
    platform::ui::hostlink::start();
    std::string endpoint;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (endpoint.empty() && std::chrono::steady_clock::now() < deadline)
    {
        std::ifstream input(sd / "hostlink/endpoint.txt");
        std::getline(input, endpoint);
        if (endpoint.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (endpoint.empty())
    {
        platform::ui::hostlink::stop();
        return 2;
    }
    std::cout << endpoint << std::endl;
    std::string command;
    std::getline(std::cin, command);
    platform::ui::hostlink::stop();
}

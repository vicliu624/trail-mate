#include "platform/linux/backlight.h"
#include "platform/ui/device_runtime.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <thread>
#include <unistd.h>

namespace
{
using namespace platform::linux_runtime;
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
void put(const std::filesystem::path& path, unsigned value) { std::ofstream(path) << value << '\n'; }
void waitFor(BacklightRuntime& runtime, const std::function<bool(const BacklightSnapshot&)>& predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (!predicate(runtime.snapshot()))
    {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("backlight condition timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
} // namespace
int main()
{
    using namespace platform::linux_runtime;
    require(!backlight_snapshot().available, "snapshot unexpectedly initialized hardware");
    char temporary[] = "/tmp/trailmate-backlight-test-XXXXXX";
    require(mkdtemp(temporary), "temporary directory failed");
    const std::filesystem::path root(temporary), first = root / "first", second = root / "second";
    std::filesystem::create_directory(first);
    put(first / "max_brightness", 4095);
    put(first / "brightness", 2048);
    BacklightOptions options;
    options.root = root;
    {
        BacklightRuntime runtime(options);
        waitFor(runtime, [](const auto& state)
                { return state.available && state.level == 128 && state.native_maximum == 4095; });
        require(runtime.request(255), "maximum request rejected");
        waitFor(runtime, [](const auto& state)
                { return state.available && state.level == 255 && !state.failed; });
        unsigned native;
        std::ifstream(first / "brightness") >> native;
        require(native == 4095, "native maximum scaling failed");
        runtime.request(0);
        waitFor(runtime, [](const auto& state)
                { return state.available && state.level == 0; });
        for (unsigned index = 1; index < 90; ++index) runtime.request(index);
        runtime.request(73);
        waitFor(runtime, [](const auto& state)
                { return state.level == 73; });
        put(first / "brightness", 2730);
        waitFor(runtime, [](const auto& state)
                { return state.level == 170; });
        if (geteuid() != 0)
        {
            std::filesystem::permissions(first / "brightness", std::filesystem::perms::owner_read);
            runtime.request(50);
            waitFor(runtime, [](const auto& state)
                    { return state.failed; });
            require(runtime.snapshot().level == 170, "failed write optimistically changed brightness");
            std::filesystem::permissions(first / "brightness", std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
        }
        std::filesystem::create_directory(second);
        put(second / "max_brightness", 255);
        put(second / "brightness", 90);
        waitFor(runtime, [](const auto& state)
                { return state.selection_required && !state.available; });
        runtime.request(20);
        waitFor(runtime, [](const auto& state)
                { return state.selection_required && state.failed; });
        std::ifstream(first / "brightness") >> native;
        require(native == 2730, "ambiguous request changed a display");
    }
    {
        options.device = second;
        BacklightRuntime runtime(options);
        waitFor(runtime, [](const auto& state)
                { return state.available && state.level == 90; });
        runtime.request(201);
        waitFor(runtime, [](const auto& state)
                { return state.level == 201 && !state.failed; });
        std::ofstream(second / "max_brightness") << "invalid\n";
        waitFor(runtime, [](const auto& state)
                { return !state.available; });
        put(second / "max_brightness", 255);
        waitFor(runtime, [](const auto& state)
                { return state.available; });
        std::filesystem::remove_all(second);
        waitFor(runtime, [](const auto& state)
                { return !state.available; });
    }
    options.device = first;
    put(first / "actual_brightness", 1000);
    require(configure_backlight(options), "global backlight configuration failed");
    require(!configure_backlight(options), "second configuration accepted");
    require(!backlight_snapshot().available, "configuration performed device I/O");
    const auto await = [](const std::function<bool()>& predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        while (!predicate())
        {
            if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("global backlight timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    };
    prepare_backlight();
    await([]
          { return backlight_snapshot().available; });
    require(platform::ui::device::supports_screen_brightness() && platform::ui::device::screen_brightness_max() == 255,
            "UI brightness port did not expose the device");
    require(platform::ui::device::screen_brightness() == 62, "actual brightness was ignored");
    platform::ui::device::set_screen_brightness(55);
    await([&]
          { unsigned value = 0; std::ifstream(first / "brightness") >> value; return value == 883; });
    require(platform::ui::device::screen_brightness() == 62, "requested level was mistaken for applied hardware level");
    put(first / "actual_brightness", 883);
    await([]
          { return platform::ui::device::screen_brightness() == 55; });
    shutdown_backlight();
    require(!platform::ui::device::supports_screen_brightness(), "shutdown retained a brightness device");
    require(!request_backlight(50), "stopped service accepted requests");
    std::filesystem::remove_all(root);
}

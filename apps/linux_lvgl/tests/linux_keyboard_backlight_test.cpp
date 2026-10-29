#include "platform/desktop/device_options.h"
#include "platform/linux/keyboard_backlight.h"
#include "platform/ui/device_runtime.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
namespace runtime = platform::linux_runtime;
void put(const fs::path& path, const std::string& value) { std::ofstream(path) << value << '\n'; }
uint64_t get(const fs::path& path)
{
    uint64_t value = 0;
    std::ifstream(path) >> value;
    return value;
}
template <class Predicate>
void await(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (!predicate())
    {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Keyboard light state timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "--device")
    {
        const auto options = platform::desktop::loadDeviceOptions(argv[2]).keyboard_backlight;
        runtime::KeyboardBacklightRuntime light(options);
        await([&]
              { return light.snapshot().available; });
        const auto original = light.snapshot().level;
        try
        {
            for (uint8_t level : {uint8_t(128), uint8_t(0), original})
            {
                if (!light.request(level)) throw std::runtime_error("Keyboard light request rejected");
                await([&]
                      { const auto state = light.snapshot(); return state.available && !state.failed && state.level == level; });
            }
        }
        catch (const std::exception& error)
        {
            light.request(original);
            try
            {
                await([&]
                      { return light.snapshot().level == original; });
            }
            catch (...)
            {
            }
            std::fprintf(stderr, "%s\n", error.what());
            return 1;
        }
        std::printf("Keyboard PWM: stable selection, dim/off and restored level=%u passed\n", original);
        return 0;
    }
    assert(argc == 1);
    const auto root = fs::temp_directory_path() / ("trailmate-keyboard-light-" + std::to_string(getpid()));
    fs::create_directories(root / "class");
    fs::create_directories(root / "tree/keyboard-light");
    fs::create_directories(root / "tree/unrelated");
    runtime::KeyboardBacklightOptions options;
    options.root = root / "class";
    options.device_tree_root = root / "tree";
    options.of_node = "/keyboard-light";
    options.initial_period_ns = 50000;
    options.max_duty_permille = 100;
    assert(runtime::valid_keyboard_backlight_options(options));
    auto invalid = options;
    invalid.of_node = "/../other";
    assert(!runtime::valid_keyboard_backlight_options(invalid));
    invalid = options;
    invalid.max_duty_permille = 0;
    assert(!runtime::valid_keyboard_backlight_options(invalid));
    const auto chip = options.root / "pwmchip27";
    fs::create_directories(chip / "device");
    fs::create_directory_symlink(root / "tree/keyboard-light", chip / "device/of_node");
    put(chip / "npwm", "1");
    fs::create_directories(chip / "pwm0");
    put(chip / "pwm0/period", "50000");
    put(chip / "pwm0/duty_cycle", "5000");
    put(chip / "pwm0/enable", "1");
    put(chip / "pwm0/polarity", "normal");
    const auto unrelated = options.root / "pwmchip0";
    fs::create_directories(unrelated / "device");
    fs::create_directory_symlink(root / "tree/unrelated", unrelated / "device/of_node");
    put(unrelated / "npwm", "1");
    {
        runtime::KeyboardBacklightRuntime light(options);
        await([&]
              { return light.snapshot().available; });
        assert(light.snapshot().level == 255 && light.snapshot().native_maximum == 5000);
        assert(light.request(128));
        await([&]
              { return light.snapshot().level == 128; });
        assert(get(chip / "pwm0/duty_cycle") == 2510);
        assert(light.request(0));
        await([&]
              { return light.snapshot().level == 0; });
        assert(get(chip / "pwm0/enable") == 0 && get(chip / "pwm0/duty_cycle") == 0);
        const auto renamed = options.root / "pwmchip99";
        fs::rename(chip, renamed);
        assert(light.request(255));
        await([&]
              { return light.snapshot().level == 255; });
        assert(get(renamed / "pwm0/duty_cycle") == 5000 && !fs::exists(unrelated / "pwm0"));
        fs::create_directory_symlink(renamed, options.root / "pwmchip100");
        await([&]
              { return light.snapshot().selection_required; });
        assert(!light.snapshot().available);
        assert(light.request(0));
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        assert(get(renamed / "pwm0/duty_cycle") == 5000);
        fs::remove(options.root / "pwmchip100");
        await([&]
              { return light.snapshot().available && light.snapshot().level == 0; });
    }
    {
        const auto late_chip = options.root / "pwmchip101";
        fs::create_directories(root / "tree/late-light");
        fs::create_directories(late_chip / "device");
        fs::create_directory_symlink(root / "tree/late-light", late_chip / "device/of_node");
        put(late_chip / "npwm", "1");
        put(late_chip / "export", "");
        auto late = options;
        late.of_node = "/late-light";
        late.export_channel = true;
        std::thread kernel([&]
                           {
                               await([&] { return fs::file_size(late_chip / "export") > 1; });
                               fs::create_directories(late_chip / "pwm0");
                               std::this_thread::sleep_for(std::chrono::milliseconds(50));
                               put(late_chip / "pwm0/period", "0"); put(late_chip / "pwm0/duty_cycle", "0");
                               put(late_chip / "pwm0/enable", "0"); put(late_chip / "pwm0/polarity", "normal"); });
        runtime::KeyboardBacklightRuntime light(late);
        kernel.join();
        await([&]
              { return light.snapshot().available; });
        assert(get(late_chip / "pwm0/period") == 50000 && get(late_chip / "pwm0/enable") == 0);
        assert(light.request(64));
        await([&]
              { return light.snapshot().level == 64; });
    }
    assert(runtime::configure_keyboard_backlight(options));
    assert(!platform::ui::device::supports_keyboard_backlight());
    runtime::prepare_keyboard_backlight();
    await([]
          { return platform::ui::device::supports_keyboard_backlight(); });
    assert(platform::ui::device::keyboard_backlight_max() == 255);
    platform::ui::device::set_keyboard_backlight(255);
    await([]
          { return platform::ui::device::keyboard_backlight() == 255; });
    runtime::shutdown_keyboard_backlight();
    assert(!platform::ui::device::supports_keyboard_backlight() && !runtime::request_keyboard_backlight(1));
    fs::remove_all(root);
}

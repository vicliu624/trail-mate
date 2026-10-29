#include "platform/linux/battery_monitor.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

using namespace platform::linux_runtime;
using namespace std::chrono_literals;
void write(const std::filesystem::path& path, const char* value)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << value << '\n';
}
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-battery-" + std::to_string(getpid()));
    write(root / "pack/type", "Battery");
    write(root / "pack/present", "1");
    write(root / "pack/capacity", "99");
    write(root / "pack/status", "Not charging");
    write(root / "charger/type", "USB");
    write(root / "charger/online", "1");
    write(root / "charger/status", "Full");
    auto value = read_power_supplies(root);
    assert(value.available && value.level == 99 && value.external_power && !value.charging);
    write(root / "charger/status", "Charging");
    value = read_power_supplies(root);
    assert(value.available && value.charging && value.external_power);
    write(root / "charger/online", "0");
    value = read_power_supplies(root);
    assert(!value.charging && !value.external_power);
    // Offline supplies must not hide another online source.
    write(root / "second-source/type", "Mains");
    write(root / "second-source/online", "1");
    assert(read_power_supplies(root).external_power);
    write(root / "peripheral/type", "Battery");
    write(root / "peripheral/scope", "Device");
    write(root / "peripheral/capacity", "2");
    write(root / "peripheral/status", "Charging");
    assert(read_power_supplies(root).level == 99);
    write(root / "charger/online", "1");
    write(root / "extra-pack/type", "Battery");
    write(root / "extra-pack/capacity", "42");
    write(root / "extra-pack/status", "Discharging");
    value = read_power_supplies(root, "pack");
    assert(value.level == 99 && !value.charging); // No cross-pack charger inference.
    assert(read_power_supplies(root, "extra-pack").level == 42);
    assert(!read_power_supplies(root, "missing").available);
    write(root / "pack/capacity", "99garbage");
    assert(!read_power_supplies(root, "pack").available);
    write(root / "pack/capacity", "9999999999999999999999");
    assert(!read_power_supplies(root, "pack").available);
    write(root / "pack/capacity", "101");
    assert(!read_power_supplies(root, "pack").available);
    write(root / "pack/capacity", "50");
    write(root / "pack/present", "0");
    assert(!read_power_supplies(root, "pack").available);
    write(root / "pack/present", "1");
    std::filesystem::remove(root / "pack/capacity");
    assert(mkfifo((root / "pack/capacity").c_str(), 0600) == 0);
    {
        BatteryMonitor monitor(root, "pack", 1h);
        std::this_thread::sleep_for(30ms);
        const auto start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < 1000; ++i) assert(!monitor.snapshot().available);
        assert(std::chrono::steady_clock::now() - start < 100ms);
        // Unblock the real filesystem reader; the UI mailbox stayed responsive.
        std::ofstream(root / "pack/capacity") << "50\n";
        const auto end = std::chrono::steady_clock::now() + 1s;
        while (!monitor.snapshot().available && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(1ms);
        assert(monitor.snapshot().level == 50);
    }
    assert(!read_power_supplies(root / "missing-root").available);
    std::filesystem::remove_all(root);
}

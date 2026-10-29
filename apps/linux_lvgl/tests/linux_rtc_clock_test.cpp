#include "platform/linux/rtc_clock.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace platform::linux_runtime;
void clock_fixture(const fs::path& root, const char* id, const char* epoch, bool source = false)
{
    fs::create_directories(root / id / "device");
    std::ofstream(root / id / "name") << "test-rtc\n";
    std::ofstream(root / id / "device/name") << "test-chip\n";
    std::ofstream(root / id / "since_epoch") << epoch << '\n';
    std::ofstream(root / id / "hctosys") << (source ? "1\n" : "0\n");
}
int main()
{
    char directory[] = "/tmp/trailmate-rtc-XXXXXX";
    assert(mkdtemp(directory));
    const fs::path root(directory);
    assert(!read_rtc_clock(root).valid);
    clock_fixture(root, "rtc7", "1792571555");
    auto sample = read_rtc_clock(root);
    assert(sample.available && sample.valid && sample.utc_seconds == 1792571555 && sample.class_name == "rtc7");
    clock_fixture(root, "rtc12", "1792571556", true);
    assert(read_rtc_clock(root).class_name == "rtc12");
    assert(read_rtc_clock(root, "rtc7").utc_seconds == 1792571555);
    assert(!read_rtc_clock(root, "test-chip").available);
    assert(!read_rtc_clock(root, "absent").available);
    clock_fixture(root, "rtc12", "bad-time", false);
    assert(!read_rtc_clock(root).valid); // No arbitrary first-device selection.
    assert(read_rtc_clock(root, "rtc12").available && !read_rtc_clock(root, "rtc12").valid);
    clock_fixture(root, "rtc12", "12trailing", true);
    assert(!read_rtc_clock(root).valid);
    clock_fixture(root, "rtc12", "-1", true);
    assert(!read_rtc_clock(root).valid);
    clock_fixture(root, "rtc12", "1792571556", true);
    {
        RtcMonitor monitor(root, "rtc12", std::chrono::milliseconds(10));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!monitor.snapshot().valid && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        assert(monitor.snapshot().valid);
        fs::remove_all(root / "rtc12");
        while (monitor.snapshot().valid && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        assert(!monitor.snapshot().valid);
    }
    fs::remove_all(root);
}

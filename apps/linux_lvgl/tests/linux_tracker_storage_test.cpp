#include "platform/ui/gps_runtime.h"
#include "platform/ui/tracker_runtime.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
namespace tracker = platform::ui::tracker;
namespace
{
uint32_t now_ms = 0;
constexpr uint32_t epoch = 1792600000;
gps::GpsState fix;
std::string read(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    assert(input.good());
    return {std::istreambuf_iterator<char>(input), {}};
}
void write(const fs::path& path, const std::string& value)
{
    std::ofstream output(path, std::ios::binary);
    output << value;
    assert(output.good());
}
std::string start(tracker::Format format)
{
    tracker::set_format(format);
    assert(tracker::start_recording());
    std::string path;
    assert(tracker::current_path(path));
    return path;
}
uint32_t le32(const std::string& value, size_t offset)
{
    uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i) result |= uint32_t(uint8_t(value.at(offset + i))) << (8 * i);
    return result;
}
} // namespace
namespace sys
{
uint32_t millis_now() { return now_ms; }
uint32_t epoch_seconds_now() { return epoch; }
} // namespace sys
namespace platform::ui::gps
{
GpsState get_data() { return fix; }
} // namespace platform::ui::gps

int main(int argc, char** argv)
{
    char pattern[] = "/tmp/trailmate-tracker-XXXXXX";
    const char* temporary = mkdtemp(pattern);
    assert(temporary);
    const fs::path root(temporary), tracks = root / "tracks";
    assert(setenv("TRAIL_MATE_SD_ROOT", temporary, 1) == 0);
    assert(tracker::is_supported());
    fix.valid = true;
    fix.lat = 12.3456789;
    fix.lng = -45.1234567;
    fix.alt_m = -12;
    fix.speed_mps = 1.25;
    fix.has_alt = fix.has_speed = true;
    fix.satellites = 7;
    tracker::set_interval_seconds(1);

    const auto gpx = start(tracker::Format::GPX);
    const std::string footer = "</trkseg></trk></gpx>\n";
    assert(read(gpx).find(footer) != std::string::npos);
    tracker::poll(); // A first point at uptime zero must not be sampled twice.
    const auto first = read(gpx);
    tracker::poll();
    assert(read(gpx) == first);
    assert(first.find("<trkpt") != std::string::npos);
    assert(first.substr(first.size() - footer.size()) == footer);
    assert(!tracker::remove_track(gpx));
    const auto child = fork();
    assert(child >= 0);
    if (child == 0)
    {
        tracker::stop_recording(); // Parent still holds the recording lock.
        _exit(tracker::remove_track(gpx) ? 1 : 0);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    tracker::set_format(tracker::Format::Binary);
    now_ms = 1000;
    tracker::poll();
    assert(read(gpx).find("TMTRK1") == std::string::npos);
    tracker::stop_recording();
    const auto complete = read(gpx);
    assert(complete.find(footer) == complete.rfind(footer));
    const auto another = start(tracker::Format::GPX);
    assert(another != gpx && read(gpx) == complete); // Same frozen second.
    tracker::stop_recording();
    assert(tracker::remove_track(gpx)); // Shared UI uses the absolute path.
    assert(tracker::remove_track(fs::path(another).filename().string()));

    const auto binary = start(tracker::Format::Binary);
    tracker::poll();
    const auto bytes = read(binary);
    assert(bytes.size() == 26 && bytes.substr(0, 6) == "TMTRK1");
    assert(le32(bytes, 6) == epoch);
    assert(int32_t(le32(bytes, 10)) == 123456789);
    assert(int32_t(le32(bytes, 14)) == -451234567);
    assert(uint8_t(bytes[18]) == 244 && uint8_t(bytes[19]) == 255);
    assert(uint8_t(bytes[20]) == 125 && bytes[21] == 0);
    assert(bytes[22] == 7 && bytes[23] == 3 && bytes[24] == 0 && bytes[25] == 0);
    fix.alt_m = std::numeric_limits<double>::quiet_NaN();
    fix.speed_mps = 1000;
    now_ms += 1000;
    tracker::poll();
    const auto absent = read(binary);
    assert(absent.size() == 46 && absent[43] == 0);
    for (size_t i = 38; i < 42; ++i) assert(absent[i] == 0);
    tracker::stop_recording();

    const auto csv = start(tracker::Format::CSV);
    tracker::poll();
    assert(read(csv).find("time,lat,lon,alt_m,speed_mps,satellites\n") == 0);
    assert(read(csv).find(",12.3456789,-45.1234567,0.00,1000.00,7\n") != std::string::npos);
    tracker::stop_recording();
    if (argc == 2)
    {
        const fs::path exported(argv[1]);
        assert(fs::is_directory(exported));
        write(exported / "recording.bin", bytes);
        write(exported / "recording.csv", read(csv));
        write(exported / "recording.gpx", complete);
    }

    tracker::set_distance_only(true);
    now_ms = UINT32_MAX - 499;
    const auto distance = start(tracker::Format::Binary);
    tracker::poll();
    now_ms = 500; // Wrapped elapsed is 1000 ms.
    fix.lat += 0.00001;
    tracker::poll();
    assert(read(distance).size() == 26); // Less than five metres.
    fix.lat += 0.0001;
    tracker::poll();
    assert(read(distance).size() == 46);
    tracker::stop_recording();
    tracker::set_distance_only(false);

    const auto renamed = start(tracker::Format::GPX);
    fs::rename(renamed, renamed + ".old");
    write(renamed, "replacement");
    tracker::poll();
    assert(!tracker::is_recording() && read(renamed) == "replacement");

    write(tracks / "active.bin", "metadata");
    write(tracks / "ACTIVE.BIN", "metadata");
    write(tracks / ".pending.gpx", "pending");
    write(tracks / "ignored.txt", "other");
    write(tracks / "UPPER.GPX", "legacy");
    write(root / "outside.gpx", "outside");
    fs::create_symlink(root / "outside.gpx", tracks / "link.gpx");
    fs::create_hard_link(root / "outside.gpx", tracks / "hard.gpx");
    assert(!tracker::remove_track("link.gpx"));
    assert(!tracker::remove_track("hard.gpx"));
    assert(!tracker::remove_track("../outside.gpx"));
    assert(!tracker::remove_track((root / "outside.gpx").string()));
    assert(!tracker::remove_track("active.bin") && !tracker::remove_track("ACTIVE.BIN"));
    std::vector<std::string> names;
    assert(tracker::list_tracks(names, 100));
    bool uppercase = false;
    for (const auto& name : names)
    {
        assert(name != "link.gpx" && name != "hard.gpx" && name != "active.bin" && name != "ACTIVE.BIN");
        assert(name != ".pending.gpx" && name != "ignored.txt");
        if (name == "UPPER.GPX") uppercase = true;
    }
    assert(uppercase);
    assert(tracker::list_tracks(names, 2) && names.size() == 2);
    assert(tracker::list_tracks(names, 0) && names.empty());
    fs::remove_all(root); // Only the mkdtemp-owned test directory.
    std::cout << "Tracker storage, formats, timing, ownership and path checks passed\n";
}

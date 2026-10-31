#include "platform/linux/oneseg_channel_store.h"
#include <cassert>
#include <iostream>

using namespace platform::linux_runtime;
int main()
{
    auto folder = std::filesystem::temp_directory_path() / ("trailmate-tv-store-" + std::to_string(getpid()));
    std::filesystem::create_directories(folder);
    const auto path = folder / "channels.txt";
    std::string error;
    OneSegChannelStore store(path);
    assert(store.load(error) && store.channels().empty());
    const std::vector<OneSegChannel> initial{{13, 101, "Station \"One\""}, {14, 102, "Station Two"}};
    assert(store.replaceScan(initial, error));
    assert(store.select(14, 102, error));
    OneSegChannelStore restart(path);
    assert(restart.load(error) && restart.lastChannel()->channel == 14);
    assert(restart.channels()[0].name == initial[0].name);
    assert(!restart.replaceScan({}, error));
    assert(!restart.replaceScan({initial[0], initial[0]}, error));
    assert(!restart.replaceScan({{63, 103, "Invalid channel"}}, error));
    assert(!restart.replaceScan({{13, 0, "Invalid service"}}, error));
    assert(!restart.replaceScan({{13, 103, "Invalid\nname"}}, error));
    assert(!restart.select(15, 999, error));
    assert(restart.lastChannel()->channel == 14);
    OneSegChannelStore disk_after_failed_scan(path);
    assert(disk_after_failed_scan.load(error));
    assert(disk_after_failed_scan.channels().size() == initial.size());
    assert(disk_after_failed_scan.lastChannel()->service == 102);
    // A rescan retains the last service where it is still present.
    assert(restart.replaceScan({initial[1], {15, 103, "New station"}}, error));
    assert(restart.lastChannel()->service == 102);
    assert(restart.rememberReceived({16, 104, "Manually tuned"}, error));
    assert(restart.lastChannel()->channel == 16);
    assert(restart.rememberReceived({16, 104, {}}, error));
    assert(restart.lastChannel()->name == "Manually tuned");
    assert(restart.select(14, 102, error));
    // A failed rename must preserve the existing file, not leave a partial file.
    const auto blocked_path = folder / "directory-target";
    std::filesystem::create_directory(blocked_path);
    OneSegChannelStore blocked(blocked_path);
    assert(!blocked.replaceScan(initial, error));
    assert(blocked.channels().empty() && std::filesystem::is_directory(blocked_path));
    for (const auto& entry : std::filesystem::directory_iterator(folder))
        assert(entry.path().filename().string().find(".tmp.") == std::string::npos);
    // Corrupt input must not partially overwrite the in-memory working list.
    {
        std::ofstream out(path);
        out << "TRAILMATE_ONESEG_V1 13 101 2\n13 101 \"Truncated\"\n";
    }
    assert(!restart.load(error));
    assert(restart.channels().size() == 3 && restart.lastChannel()->service == 102);
    // A failed filesystem transaction retains the prior in-memory selection.
    const auto moved = folder.string() + "-moved";
    std::filesystem::rename(folder, moved);
    {
        std::ofstream blocker(folder);
        blocker << "not a directory";
    }
    assert(!restart.select(15, 103, error));
    assert(restart.lastChannel()->service == 102);
    std::filesystem::remove(folder);
    std::filesystem::rename(moved, folder);
    std::filesystem::remove_all(folder);
    std::cout << "TV channel persistence: restart, rescan, corrupt input and failed writes passed\n";
}

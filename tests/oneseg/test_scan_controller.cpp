#include "platform/linux/oneseg_scan_controller.h"
#include <cassert>
#include <iostream>

using namespace platform::linux_runtime;
// TEST FIXTURE - NO RF: deterministic observations and explicit release gates.
struct TestPort final : OneSegScanPort
{
    Observation current;
    std::vector<unsigned> tuned, selected;
    unsigned stops = 0;
    Observation observe() const override { return current; }
    bool start(unsigned channel) override
    {
        assert(!current.active); // No overlapping receive ownership.
        tuned.push_back(channel);
        current = {};
        current.channel = channel;
        current.active = true;
        return true;
    }
    void stop() override { ++stops; } // Owner releases only when the test says so.
    bool select(unsigned programme) override
    {
        selected.push_back(programme);
        current.programme = programme;
        current.confirmed_media = false;
        return true;
    }
    void picture(unsigned programme, std::vector<unsigned> programmes)
    {
        current.programme = programme;
        current.programmes = std::move(programmes);
        current.confirmed_media = true;
    }
};
using Scan = OneSegScanController;
int main()
{
    auto pattern = (std::filesystem::temp_directory_path() / "trailmate-scan-XXXXXX").string();
    const char* created = mkdtemp(pattern.data());
    assert(created);
    const std::filesystem::path root(created), path = root / "channels.txt";
    OneSegChannelStore saved(path);
    const std::vector<OneSegChannel> old{{20, 1, "Previously saved"}};
    std::string error;
    assert(saved.replaceScan(old, error));
    {
        TestPort port;
        Scan scan(port, saved, {13, 14, 100, 100});
        assert(scan.start());
        scan.tick(0);
        port.picture(8, {8, 9});
        scan.tick(1);
        assert(scan.snapshot().found == 1 && saved.lastChannel()->channel == 20);
        assert(port.selected == std::vector<unsigned>{9});
        port.picture(9, {8, 9});
        scan.tick(2);
        assert(scan.snapshot().found == 2 && scan.snapshot().phase == Scan::Phase::Stopping);
        scan.tick(3);
        assert(port.tuned.size() == 1); // Wait for physical release before retuning.
        port.current.active = false;
        scan.tick(4);
        assert(port.tuned == (std::vector<unsigned>{13, 14}));
        port.current.active = false;
        port.current.no_signal = true;
        port.current.failed = true;
        scan.tick(5);
        scan.tick(6);
        assert(scan.snapshot().phase == Scan::Phase::Completed && scan.snapshot().scanned == 2);
        assert(saved.channels().size() == 2 && saved.channels()[1].service == 9);
        OneSegChannelStore reloaded(path);
        assert(reloaded.load(error) && reloaded.channels().size() == 2);
    }
    for (bool restore_failure : {false, true})
    {
        assert(saved.replaceScan(old, error));
        TestPort port;
        Scan scan(port, saved, {13, 13, 100, 100});
        assert(scan.start());
        scan.tick(0);
        port.picture(8, {8});
        scan.tick(1);
        if (!restore_failure) scan.cancel();
        port.current.active = false;
        port.current.restoration_failed = restore_failure;
        scan.tick(2);
        assert(scan.snapshot().phase == (restore_failure ? Scan::Phase::Error : Scan::Phase::Cancelled));
        assert(saved.channels().size() == 1 && saved.lastChannel()->channel == 20);
    }
    {
        TestPort port;
        Scan scan(port, saved, {13, 14, 100, 100});
        assert(scan.start());
        scan.tick(0);
        port.current.active = false;
        port.current.no_signal = true;
        scan.tick(1);
        scan.tick(2);
        port.current.active = false;
        port.current.no_signal = true;
        scan.tick(3);
        scan.tick(4);
        assert(scan.snapshot().phase == Scan::Phase::Completed && scan.snapshot().found == 0);
        assert(saved.lastChannel()->channel == 20);
    }
    {
        TestPort port;
        Scan scan(port, saved, {13, 13, 100, 100});
        assert(scan.start());
        scan.tick(0);
        port.picture(8, {8, 9});
        scan.tick(1);
        scan.tick(102); // Programme 9 never produced confirmed media.
        port.current.active = false;
        scan.tick(103);
        assert(scan.snapshot().phase == Scan::Phase::Completed);
        assert(saved.channels().size() == 1 && saved.channels()[0].service == 8);
    }
    {
        assert(saved.replaceScan(old, error));
        TestPort port;
        Scan scan(port, saved, {13, 13, 100, 100});
        assert(scan.start());
        scan.tick(0);
        port.current.active = false;
        port.current.failed = true;
        port.current.message = "Backend unavailable";
        scan.tick(1);
        scan.tick(2);
        assert(scan.snapshot().phase == Scan::Phase::Error && saved.lastChannel()->channel == 20);
    }
    {
        TestPort port;
        Scan scan(port, saved, {14, 13, 100, 100});
        assert(!scan.start() && port.tuned.empty() && port.stops == 0);
    }
    {
        // A successful picture must not turn a later cleanup failure into success.
        TestPort port;
        Scan scan(port, saved, {13, 14, 100, 100});
        assert(scan.start());
        scan.tick(0);
        port.picture(8, {8});
        scan.tick(1);
        port.current.active = false;
        port.current.failed = true;
        port.current.message = "Decoder cleanup failed";
        scan.tick(2);
        assert(scan.snapshot().phase == Scan::Phase::Error);
        assert(port.tuned == std::vector<unsigned>{13});
        assert(saved.lastChannel()->channel == 20);
    }
    {
        // Replace the destination with a directory to force atomic rename failure,
        // independently of root privileges in the test environment.
        const auto blocked_path = root / "blocked.txt";
        OneSegChannelStore blocked(blocked_path);
        assert(blocked.replaceScan(old, error));
        std::filesystem::remove(blocked_path);
        std::filesystem::create_directory(blocked_path);
        TestPort port;
        Scan scan(port, blocked, {13, 13, 100, 100});
        assert(scan.start());
        scan.tick(0);
        port.picture(8, {8});
        scan.tick(1);
        port.current.active = false;
        scan.tick(2);
        assert(scan.snapshot().phase == Scan::Phase::Error);
        assert(blocked.channels().size() == 1 && blocked.lastChannel()->channel == 20);
        for (const auto& entry : std::filesystem::directory_iterator(root))
            assert(entry.path().filename().string().find("blocked.txt.tmp.") != 0);
    }
    std::filesystem::remove_all(root);
    std::cout << "Scan transaction: received media, programme enumeration, release gates, no signal, empty/cancel/failure retention passed\n";
}

#include "platform/linux/packaged_map.h"
#include "platform/linux/tmap_creation.h"
#include "platform/ui/map_search_runtime.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace tc = platform::ui::tmap_creation;
namespace native = platform::linux_runtime;
using namespace std::chrono_literals;
void exercise_restart(const std::filesystem::path& root, const char* producer)
{
    const auto storage = root / "sd", jobs = storage / "trailmate/.tmap-jobs";
    const pid_t application = fork();
    assert(application >= 0);
    if (application == 0)
    {
        native::LinuxTmapCreation service({storage, root / "cache", producer});
        const auto until = std::chrono::steady_clock::now() + 10s;
        while (service.sources().empty())
        {
            if (std::chrono::steady_clock::now() >= until) _exit(2);
            std::this_thread::sleep_for(2ms);
        }
        tc::Plan plan;
        plan.name = "Cancel";
        plan.source = service.sources().front().path;
        plan.filename = "interrupted.tmap";
        plan.package_key = "interrupted";
        plan.west = 102.70;
        plan.east = 102.71;
        plan.south = 25;
        plan.north = 25.01;
        plan.min_zoom = plan.max_zoom = 13;
        std::string error;
        if (!service.start(plan, error)) _exit(3);
        for (;;) pause();
    }
    std::filesystem::path interrupted;
    const auto until = std::chrono::steady_clock::now() + 15s;
    while (interrupted.empty())
    {
        assert(std::chrono::steady_clock::now() < until);
        if (std::filesystem::is_directory(jobs))
            for (const auto& entry : std::filesystem::directory_iterator(jobs))
                if (entry.is_directory() && std::filesystem::is_regular_file(entry.path() / "producer.pid")) interrupted = entry.path();
        std::this_thread::sleep_for(5ms);
    }
    pid_t producer_pid = 0;
    std::ifstream(interrupted / "producer.pid") >> producer_pid;
    assert(producer_pid > 0);
    assert(kill(application, SIGKILL) == 0);
    int status = 0;
    assert(waitpid(application, &status, 0) == application && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    // Model an interrupted publication and malicious-looking links without
    // touching a real map or source. Recovery must stay descriptor-relative.
    std::filesystem::create_directories(interrupted / "publish/maps/tmap/osm");
    std::ofstream(interrupted / "publish/maps/tmap/osm/partial.tmap") << "partial";
    std::filesystem::create_directory_symlink(storage / "map-sources", interrupted / "source-link");
    std::filesystem::create_directories(storage / "maps/tmap/osm");
    const auto final = storage / "maps/tmap/osm/preserved.tmap";
    const auto unknown = storage / "maps/tmap/osm/unknown.creating";
    std::ofstream(final) << "finished map";
    std::ofstream(unknown) << "not owned by recovery";
    std::ofstream(jobs / "unrelated") << "leave alone";
    {
        native::LinuxTmapCreation restarted({storage, root / "cache", producer});
        const auto ready = std::chrono::steady_clock::now() + 10s;
        while (!restarted.snapshot().source_revision)
        {
            assert(std::chrono::steady_clock::now() < ready);
            std::this_thread::sleep_for(2ms);
        }
        assert(std::filesystem::exists(interrupted)); // Orphan producer holds the inherited lease.
        assert(kill(producer_pid, SIGKILL) == 0);
        while (std::filesystem::exists(interrupted))
        {
            assert(std::chrono::steady_clock::now() < ready);
            std::this_thread::sleep_for(5ms);
        }
        std::string value;
        std::getline(std::ifstream(final), value);
        assert(value == "finished map");
        assert(std::filesystem::is_regular_file(unknown));
        assert(std::filesystem::is_regular_file(storage / "map-sources/test.osm.pbf"));
        assert(std::filesystem::is_regular_file(jobs / "unrelated"));
    }
    std::filesystem::remove(unknown);
    std::filesystem::remove(jobs / "unrelated");
    std::cout << "SIGKILL restart: orphan protected, private staging recovered, PBF/final/unknown files preserved\n";
}
int main(int argc, char** argv)
{
    assert(argc == 4);
    const std::string scenario(argv[3]);
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-tmap-creation-" + std::to_string(getpid()));
    std::filesystem::create_directories(root / "sd/map-sources");
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    std::filesystem::copy_file(argv[2], root / "sd/map-sources/test.osm.pbf");
    if (scenario == "success") exercise_restart(root, argv[1]);
    {
        native::LinuxTmapCreation service({root / "sd", root / "cache", argv[1]});
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (service.sources().empty())
        {
            assert(std::chrono::steady_clock::now() < deadline);
            std::this_thread::sleep_for(2ms);
        }
        tc::Plan plan;
        plan.source = service.sources().front().path;
        plan.name = scenario == "cancel" ? "Cancel" : scenario == "failure" ? "Fail"
                                                                            : "Test area";
        plan.filename = "test.tmap";
        plan.package_key = "test-area";
        plan.west = 102.70;
        plan.south = 25;
        plan.east = 102.71;
        plan.north = 25.01;
        plan.min_zoom = plan.max_zoom = 13;
        assert(service.snapshot().producer_ready && service.estimate(plan).valid);
        auto bad = plan;
        bad.east = bad.west;
        assert(!service.estimate(bad).valid);
        bad = plan;
        bad.filename = "../escape.tmap";
        assert(!service.estimate(bad).valid);
        bad = plan;
        bad.max_zoom = 18;
        bad.maximum_bytes = 1;
        assert(!service.estimate(bad).valid);
        if (scenario == "existing")
        {
            std::filesystem::create_directories(root / "sd/maps/tmap/osm");
            std::ofstream(root / "sd/maps/tmap/osm/test-osm.tmap") << "preserve";
        }
        std::string error;
        assert(service.start(plan, error));
        assert(!service.start(plan, error));
        if (scenario == "cancel")
        {
            const auto until = std::chrono::steady_clock::now() + 5s;
            while (service.snapshot().phase != tc::Phase::Building && service.snapshot().busy)
            {
                assert(std::chrono::steady_clock::now() < until);
                std::this_thread::sleep_for(1ms);
            }
            service.cancel();
        }
        const auto until = std::chrono::steady_clock::now() + 120s;
        while (service.snapshot().busy)
        {
            assert(std::chrono::steady_clock::now() < until);
            std::this_thread::sleep_for(5ms);
        }
        const auto result = service.snapshot();
        const auto expected = scenario == "cancel" ? tc::Phase::Cancelled : scenario == "failure" || scenario == "existing" ? tc::Phase::Failed
                                                                                                                            : tc::Phase::Succeeded;
        if (result.phase != expected) std::cerr << result.stage << ": " << result.error << '\n';
        assert(result.phase == expected);
        assert(std::filesystem::is_empty(root / "cache"));
        assert(std::filesystem::is_regular_file(plan.source));
        for (const auto& entry : std::filesystem::directory_iterator(root / "sd/trailmate/.tmap-jobs"))
            assert(entry.path().filename() == ".owner");
        if (std::filesystem::exists(root / "sd/maps"))
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root / "sd/maps"))
                if (entry.is_regular_file()) assert(entry.path().extension() == ".tmap");
        const auto output = root / "sd/maps/tmap/osm/test-osm.tmap";
        if (scenario == "existing")
        {
            std::ifstream file(output);
            std::string value;
            file >> value;
            assert(value == "preserve");
        }
        else if (expected != tc::Phase::Succeeded) assert(!std::filesystem::exists(output));
        else assert(std::filesystem::is_regular_file(output));
        if (scenario == "real")
        {
            native::PackagedMapSession session(root / "sd");
            const auto ready = std::chrono::steady_clock::now() + 10s;
            while (session.snapshot().mode == native::PackagedMapMode::Pending)
            {
                assert(std::chrono::steady_clock::now() < ready);
                std::this_thread::sleep_for(2ms);
            }
            assert(session.snapshot().mode == native::PackagedMapMode::Tmap);
            // The production SDK reader, not the producer, must discover the
            // created map and decode a tile covering this PBF fixture.
            ui::map_tiles::MapTileRef ref{ui::map_tiles::MapTileLayer::Osm, 13, 6433, 3507};
            const auto tile_deadline = std::chrono::steady_clock::now() + 10s;
            while (!session.request(ref))
            {
                assert(std::chrono::steady_clock::now() < tile_deadline);
                std::this_thread::sleep_for(2ms);
            }
            assert(platform::ui::map_search::submit("Test Spring", 25.005, 102.705));
            platform::ui::map_search::Snapshot search;
            std::array<platform::ui::map_search::Result, platform::ui::map_search::kMaxResults> results{};
            const auto search_deadline = std::chrono::steady_clock::now() + 10s;
            do
            {
                assert(std::chrono::steady_clock::now() < search_deadline);
                std::this_thread::sleep_for(2ms);
                platform::ui::map_search::poll(search, results.data(), results.size());
            } while (search.status == platform::ui::map_search::Status::Searching || search.status == platform::ui::map_search::Status::Idle);
            assert(search.status == platform::ui::map_search::Status::Ready && search.count > 0);
            platform::ui::map_search::cancel();
        }
    }
    std::filesystem::remove_all(root);
}

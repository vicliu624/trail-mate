#include "platform/linux/packaged_map.h"
#include "platform/linux/tmap_area_search.h"
#include "platform/linux/tmap_creation.h"
#include "platform/ui/map_search_runtime.h"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>
#include <unistd.h>

namespace tc = platform::ui::tmap_creation;
namespace native = platform::linux_runtime;
using namespace std::chrono_literals;
template <class Predicate>
void wait_for(Predicate predicate, std::chrono::seconds limit = 10s)
{
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!predicate())
    {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(5ms);
    }
}
int main(int argc, char** argv)
{
    assert(argc == 3);
    const std::string endpoint = argv[1];
    {
        native::TmapAreaSearch search(endpoint + "/search");
        assert(!search.search(""));
        assert(search.search("ambiguous"));
        wait_for([&]
                 { return !search.snapshot().busy; });
        auto result = search.snapshot();
        if (!result.error.empty()) std::cerr << result.error << '\n';
        assert(result.error.empty() && result.results.size() == 2);
        assert(result.results[0].country == "CN" && result.results[1].country == "US");
        assert(result.results[0].west == 102.70 && result.results[0].south == 25 && result.results[0].north == 25.01);
        assert(result.results[0].description != result.results[1].description);
        assert(search.search("ambiguous"));
        wait_for([&]
                 { return !search.snapshot().busy; });
        assert(search.snapshot().from_cache);
        assert(search.search("malformed"));
        wait_for([&]
                 { return !search.snapshot().busy; });
        assert(!search.snapshot().error.empty() && search.snapshot().results.empty());
        assert(search.search("point-only"));
        wait_for([&]
                 { return !search.snapshot().busy; });
        assert(!search.snapshot().error.empty() && search.snapshot().results.empty());
        assert(search.search("slow"));
        std::this_thread::sleep_for(1100ms);
        assert(search.search("ambiguous"));
        wait_for([&]
                 { return !search.snapshot().busy; });
        assert(search.snapshot().query == "ambiguous" && search.snapshot().results.size() == 2);
    }
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-tmap-online-" + std::to_string(getpid()));
    std::filesystem::create_directories(root / "sd");
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    setenv("TRAIL_MATE_AREA_SEARCH_URL", (endpoint + "/search").c_str(), 1);
    tc::Plan plan;
    plan.download_source = true;
    plan.name = "Online test area";
    plan.package_key = "online-test";
    plan.filename = "online-test.tmap";
    plan.west = 102.70;
    plan.south = 25;
    plan.east = 102.71;
    plan.north = 25.01;
    plan.min_zoom = plan.max_zoom = 13;
    plan.maximum_bytes = 128ULL * 1024 * 1024;
    const auto run = [&](const std::string& scenario, tc::Phase expected)
    {
        const auto storage = root / scenario;
        std::filesystem::create_directories(storage);
        native::TmapCreationOptions options{storage, root / "cache", argv[2]};
        options.map_endpoint = endpoint + "/map-" + scenario;
        native::LinuxTmapCreation service(options);
        wait_for([&]
                 { return service.snapshot().source_revision != 0; });
        assert(service.sources().empty()); // No user-supplied PBF is required.
        assert(service.snapshot().producer_ready && service.snapshot().downloader_ready);
        assert(service.estimate(plan).valid);
        auto too_large = plan;
        too_large.east += 2;
        too_large.north += 2;
        assert(!service.estimate(too_large).valid);
        std::string error;
        assert(service.start(plan, error));
        assert(!service.start(plan, error));
        if (scenario == "cancel")
        {
            wait_for([&]
                     { return service.snapshot().stage.find("Downloading") != std::string::npos; });
            service.cancel();
        }
        wait_for([&]
                 { return !service.snapshot().busy; },
                 120s);
        const auto result = service.snapshot();
        if (result.phase != expected) std::cerr << scenario << ": " << result.stage << ": " << result.error << '\n';
        assert(result.phase == expected);
        for (const auto& file : std::filesystem::directory_iterator(storage / "trailmate/.tmap-jobs"))
            assert(file.path().filename() == ".owner");
        return storage;
    };
    const auto storage = run("success", tc::Phase::Succeeded);
    setenv("TRAIL_MATE_SD_ROOT", storage.c_str(), 1);
    {
        native::PackagedMapSession session(storage);
        wait_for([&]
                 { return session.snapshot().mode != native::PackagedMapMode::Pending; });
        assert(session.snapshot().mode == native::PackagedMapMode::Tmap);
        ui::map_tiles::MapTileRef ref{ui::map_tiles::MapTileLayer::Osm, 13, 6433, 3507};
        wait_for([&]
                 { return session.request(ref); });
        assert(platform::ui::map_search::submit("Test Spring", 25.005, 102.705));
        platform::ui::map_search::Snapshot search;
        std::array<platform::ui::map_search::Result, platform::ui::map_search::kMaxResults> results{};
        wait_for([&]
                 {
            platform::ui::map_search::poll(search, results.data(), results.size());
            return search.status != platform::ui::map_search::Status::Idle && search.status != platform::ui::map_search::Status::Searching; });
        assert(search.status == platform::ui::map_search::Status::Ready && search.count > 0);
        platform::ui::map_search::cancel();
    }
    run("failure", tc::Phase::Failed);
    run("empty", tc::Phase::Failed);
    run("oversize", tc::Phase::Failed);
    run("cancel", tc::Phase::Cancelled);
    std::filesystem::remove_all(root);
}

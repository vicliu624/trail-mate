#include "platform/ui/map_search_runtime.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
namespace search = platform::ui::map_search;
using Results = std::array<search::Result, search::kMaxResults>;
search::Snapshot finish(Results& results)
{
    search::Snapshot snapshot;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do
    {
        search::poll(snapshot, results.data(), results.size());
        if (snapshot.status != search::Status::Idle && snapshot.status != search::Status::Searching) return snapshot;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    assert(false && "Map search fixture timed out");
    return snapshot;
}
int main(int argc, char** argv)
{
    assert(argc == 2);
    char pattern[] = "/tmp/trailmate-map-search-XXXXXX";
    const char* temporary = mkdtemp(pattern);
    assert(temporary && setenv("TRAIL_MATE_SD_ROOT", temporary, 1) == 0);
    const fs::path root(temporary);
    fs::create_directories(root / "maps/tmap/osm");
    fs::copy_file(argv[1], root / "maps/tmap/osm/search.tmap");
    Results results;
    assert(!search::submit("", 0, 0));
    assert(!search::submit("  ", 0, 0));
    assert(!search::submit("camp", std::numeric_limits<double>::quiet_NaN(), 0));
    assert(!search::submit("camp", 91, 0));
    assert(search::submit("CaMP", 0, 0));
    const auto snapshot = finish(results);
    assert(snapshot.status == search::Status::Ready && !snapshot.incomplete);
    assert(snapshot.packages == 1 && snapshot.count == 2);
    assert(std::strcmp(results[0].name, "West Camp 起点") == 0);
    assert(results[0].latitude_e7 == 0 && results[0].longitude_e7 == 0);
    assert(results[0].distance_m < 1);
    assert(std::strcmp(results[0].administrative_path, "Country / Province / West") == 0);
    assert(results[0].minimum_zoom == 16 && results[0].maximum_zoom == 16);
    assert(search::submit("终点", 0.001, 0.001));
    assert(finish(results).count == 1 && std::strcmp(results[0].name, "East Camp 终点") == 0);

    // A replacement query must win even if an earlier worker is still reading.
    for (unsigned index = 0; index < 20; ++index) assert(search::submit("west", 0, 0));
    assert(search::submit("east", 0, 0));
    assert(finish(results).count == 1 && std::strcmp(results[0].name, "East Camp 终点") == 0);
    search::cancel();
    search::Snapshot cancelled;
    assert(search::poll(cancelled, results.data(), results.size()) && cancelled.status == search::Status::Idle && cancelled.count == 0);

    const auto token = search::submitRegion(0.001, 0.002);
    assert(token);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    search::RegionResult region;
    while (search::pollRegion(token, region) && region.status == search::Status::Searching)
    {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(region.status == search::Status::Ready && region.approximate);
    assert(std::strcmp(region.path, "Country / Province") == 0);
    fs::rename(root, root.string() + ".removed");
    fs::create_directories(root / "maps/tmap/osm");
    assert(search::pollRegion(token, region) && region.status == search::Status::Error);
    search::cancel();
    assert(!search::pollRegion(token, region));
    assert(search::submit("camp", 0, 0));
    assert(finish(results).status == search::Status::NoMaps);
    std::ofstream(root / "maps/tmap/osm/broken.tmap") << "broken";
    assert(search::submit("camp", 0, 0));
    auto broken = finish(results);
    assert(broken.status == search::Status::Error && broken.incomplete);
    fs::copy_file(argv[1], root / "maps/tmap/osm/good.tmap");
    assert(search::submit("camp", 0, 0));
    const auto partial = finish(results);
    assert(partial.status == search::Status::Ready && partial.incomplete && partial.count == 2);
    search::cancel();
    fs::remove_all(root);
    fs::remove_all(root.string() + ".removed"); // Both belong to this mkdtemp fixture.
    std::cout << "Native POI search, Unicode, ranking, query replacement, regions and media checks passed\n";
}

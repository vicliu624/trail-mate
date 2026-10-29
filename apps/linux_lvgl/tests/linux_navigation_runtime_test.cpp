#include "platform/linux/tmap_storage.h"
#include "platform/ui/navigation_runtime.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace fs = std::filesystem;
namespace nav = platform::ui::navigation;
namespace native = platform::linux_runtime;
namespace
{
nav::Snapshot finish()
{
    nav::Snapshot snapshot;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    do
    {
        nav::poll(snapshot);
        if (snapshot.status != nav::Status::Idle && snapshot.status != nav::Status::Calculating) return snapshot;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    assert(false && "Navigation fixture timed out");
    return snapshot;
}
void cancelled()
{
    nav::cancel();
    nav::Request invalid;
    // No result is needed after cancel; the next submit retires completed jobs.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
}
void write(const fs::path& path, const char* data)
{
    std::ofstream output(path);
    output << data;
    assert(output.good());
}
} // namespace
int main(int argc, char** argv)
{
    assert(argc >= 2);
    char pattern[] = "/tmp/trailmate-navigation-XXXXXX";
    const char* temporary = mkdtemp(pattern);
    assert(temporary);
    const fs::path root(temporary);
    assert(setenv("TRAIL_MATE_SD_ROOT", temporary, 1) == 0);
    fs::create_directories(root / "maps/navigation");
    fs::copy_file(argv[1], root / "maps/navigation/junction.tmap");
    nav::Request request;
    std::strcpy(request.origin.name, "Origin/起点");
    std::strcpy(request.destination.name, "Destination");
    request.destination.longitude_e7 = 20000;
    assert(nav::supported());
    nav::Request invalid = request;
    invalid.origin.latitude_e7 = 900000001;
    assert(!nav::submit(invalid));
    std::memset(invalid.origin.name, 'x', sizeof(invalid.origin.name));
    assert(!nav::submit(invalid));
    assert(nav::submit(request));
    assert(!nav::submit(request));
    auto snapshot = finish();
    assert(snapshot.status == nav::Status::Ready);
    assert(snapshot.points >= 4 && snapshot.distance_m > 300 && snapshot.distance_m < 400);
    assert(fs::exists(snapshot.path));
    std::vector<std::string> names;
    assert(nav::list(names) && names.empty()); // Pending files never enter the list.
    assert(!nav::remove(fs::path(snapshot.path).filename().c_str()));
    const auto pending = std::string(snapshot.path);
    assert(nav::save());
    assert(nav::poll(snapshot) && snapshot.status == nav::Status::Saved);
    assert(!fs::exists(pending) && fs::exists(snapshot.path));
    assert(std::string(snapshot.path).find("Origin_") != std::string::npos);
    const auto saved = std::string(snapshot.path);
    assert(!nav::save());
    assert(nav::list(names) && names.size() == 1);
    // Read the real archive with the production SDK: the prohibited turn must
    // take the three-leg detour, not a straight line between the requested POIs.
    native::TmapFile archive(root);
    assert(archive.open('/' + fs::path(saved).lexically_relative(root).generic_string()));
    auto workspace = std::make_unique<tmap::RouteArchiveWorkspace>();
    tmap::RouteArchiveReader reader(*workspace);
    assert(reader.open(archive) == tmap::Result::Ok);
    assert(reader.info().legs == 3 && reader.info().points == snapshot.points);
    auto validation = std::make_unique<tmap::RouteArchiveValidationCursor>();
    auto valid_archive = reader.beginValidation(*validation);
    while (valid_archive == tmap::Result::Ok || valid_archive == tmap::Result::More)
    {
        valid_archive = reader.validationStep(*validation, 64);
        if (valid_archive == tmap::Result::Ok) break;
    }
    assert(valid_archive == tmap::Result::Ok);
    archive.close();
    cancelled();
    assert(fs::exists(saved));

    auto reverse = request;
    std::swap(reverse.origin, reverse.destination);
    assert(nav::submit(reverse));
    assert(finish().status == nav::Status::NoPath);
    cancelled();
    auto outside = request;
    outside.origin.latitude_e7 = 800000000;
    assert(nav::submit(outside));
    assert(finish().status == nav::Status::OutsideCoverage);
    cancelled();

    fs::remove(root / "maps/navigation/junction.tmap");
    assert(nav::submit(request));
    assert(finish().status == nav::Status::NoRoutingData);
    cancelled();
    write(root / "maps/navigation/corrupt.tmap", "corrupt");
    assert(nav::submit(request));
    assert(finish().status == nav::Status::NoRoutingData);
    cancelled();

    fs::create_symlink(saved, root / "routes/navigation/link.tmroute");
    write(root / "routes/navigation/metadata.bin", "private");
    assert(!nav::remove("link.tmroute") && !nav::remove("../elsewhere.kml"));
    assert(!nav::remove("metadata.bin") && !nav::remove(saved.c_str()));
    assert(nav::list(names) && names.size() == 1);
    assert(nav::list(names, 0) && names.empty());
    assert(nav::remove(fs::path(saved).filename().c_str()));
    assert(!fs::exists(saved));
    fs::remove(root / "routes/navigation/link.tmroute");
    fs::remove(root / "routes/navigation/metadata.bin");

    fs::copy_file(argv[1], root / "maps/navigation/valid.tmap");
    assert(nav::submit(request));
    const auto ready = finish();
    assert(ready.status == nav::Status::Ready);
    const auto displaced = std::string(ready.path) + ".displaced";
    fs::rename(ready.path, displaced);
    write(ready.path, "replacement");
    assert(!nav::save());
    cancelled();
    assert(fs::file_size(ready.path) == std::strlen("replacement"));
    fs::remove(ready.path);
    fs::remove(displaced);

    assert(nav::submit(request));
    nav::cancel();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!nav::submit(request))
    {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(finish().status == nav::Status::Ready);
    cancelled();
    for (const auto& entry : fs::directory_iterator(root / "routes/navigation"))
        assert(entry.path().filename().string().front() != '.');
    fs::remove_all(root); // The mkdtemp-owned fixture directory only.
    std::cout << "Native navigation planning, detour archive, publication, cancellation and storage checks passed\n";
}

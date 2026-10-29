#include "platform/linux/packaged_map.h"
#include "platform/linux/tmap_storage.h"
#include "ui_map_runtime/map_poi/poi_types.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
namespace native = platform::linux_runtime;
namespace mt = ui::map_tiles;
using Tile = std::shared_ptr<const native::PackagedMapTile>;
native::PackagedMapSnapshot prepared(native::PackagedMapSession& session, uint32_t after = 0)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;)
    {
        const auto snapshot = session.snapshot();
        if (snapshot.mode != native::PackagedMapMode::Pending && snapshot.session > after) return snapshot;
        if (std::chrono::steady_clock::now() >= end) std::cerr << "prepare timeout mode=" << unsigned(snapshot.mode) << " session=" << snapshot.session << " after=" << after << '\n';
        assert(std::chrono::steady_clock::now() < end);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
Tile read(native::PackagedMapSession& session, mt::MapTileRef ref)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;)
    {
        if (auto tile = session.request(ref)) return tile;
        assert(std::chrono::steady_clock::now() < end);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
int main(int argc, char** argv)
{
    assert(argc == 2 && native::PackagedMapSession::pixelBytes() == 0);
    char pattern[] = "/tmp/trailmate-packaged-map-XXXXXX";
    const char* temporary = mkdtemp(pattern);
    assert(temporary);
    const fs::path root(temporary), directory = root / "maps/tmap/osm", package = directory / "fixture.tmap";
    fs::create_directories(directory);
    {
        constexpr uint64_t offset = UINT64_C(3) * 1024 * 1024 * 1024;
        std::ofstream file(root / "large.bin", std::ios::binary);
        file.seekp(offset);
        file.write("64", 2);
        assert(file.good());
        file.close();
        native::TmapFile large(root);
        assert(large.open("/large.bin") && large.size() == offset + 2);
        uint8_t bytes[2]{};
        assert(large.readAt(offset, bytes, sizeof(bytes)) == tmap::Status::Ok && bytes[0] == '6' && bytes[1] == '4');
        large.close();
        fs::remove(root / "large.bin"); // Sparse fixture: only the final page contains data.
    }
    fs::copy_file(argv[1], package);
    Tile retained;
    mt::MapTileRef ref{mt::MapTileLayer::Osm, 18, 131072, 131072};
    {
        native::PackagedMapSession session(root);
        const auto snapshot = prepared(session);
        assert(snapshot.mode == native::PackagedMapMode::Tmap);
        assert(snapshot.coverage[0] == (1U << 18) && snapshot.coverage[2] == (1U << 18));
        auto base = read(session, ref);
        assert(base->result.status == mt::MapTileReadStatus::Ready && base->result.format == mt::MapTileFormat::Rgb565);
        assert(base->result.size == 131072 && base->data()[0] == 0 && base->data()[1] == 248);
        assert(read(session, ref) == base);
        auto satellite_ref = ref;
        satellite_ref.layer = mt::MapTileLayer::Satellite;
        auto satellite = read(session, satellite_ref);
        assert(satellite->result.format == mt::MapTileFormat::Rgba8888 && satellite->result.size == 262144);
        assert(satellite->data()[0] == 11 && satellite->data()[1] == 22 && satellite->data()[2] == 33 && satellite->data()[3] == 255);
        auto contour_ref = ref;
        contour_ref.layer = mt::MapTileLayer::ContourMajor25;
        auto contour = read(session, contour_ref);
        assert(contour->result.status == mt::MapTileReadStatus::Ready && contour->data()[3] == 0);
        assert(contour->data()[(64 * 256 + 128) * 4 + 2] == 255 && contour->data()[(64 * 256 + 128) * 4 + 3] == 128);
        auto poi_ref = ref;
        poi_ref.layer = mt::MapTileLayer::Poi;
        auto poi = read(session, poi_ref);
        assert(poi->result.status == mt::MapTileReadStatus::Ready && ui::map_poi::validPayload(poi->data(), poi->result.size));
        const auto* header = reinterpret_cast<const ui::map_poi::TileHeader*>(poi->data());
        assert(header->count == 2 && !header->partial);
        const auto* records = ui::map_poi::payloadRecords(poi->data());
        assert(records[0].kind == ui::map::AnnotationKind::Poi);
        assert(records[1].kind == ui::map::AnnotationKind::Road && records[1].path_points == 2);
        assert(records[1].path[0] >= 0 && records[1].path[2] > records[1].path[0]);
        auto missing = ref;
        missing.x += 100;
        assert(read(session, missing)->result.status == mt::MapTileReadStatus::Missing);
        auto invalid = ref;
        invalid.z = 31;
        assert(session.request(invalid)->result.status == mt::MapTileReadStatus::Invalid);
        retained = base;
        session.cancel();
        assert(retained->data()[1] == 248); // Display ownership survives cache cancellation.
        assert(read(session, ref)->result.status == mt::MapTileReadStatus::Ready);
        const auto before = session.snapshot().session;
        fs::rename(package, directory / "removed.bin");
        assert(prepared(session, before).mode == native::PackagedMapMode::Legacy);
        fs::rename(directory / "removed.bin", package);
        assert(prepared(session, before + 1).mode == native::PackagedMapMode::Tmap);
    }
    assert(native::PackagedMapSession::pixelBytes() > 0 && retained->data()[1] == 248);
    retained.reset();
    assert(native::PackagedMapSession::pixelBytes() == 0);
    {
        native::PackagedMapSession limited(root, 160 * 1024);
        assert(prepared(limited).mode == native::PackagedMapMode::Tmap);
        auto first = read(limited, ref);
        auto second_ref = ref;
        --second_ref.x;
        assert(!limited.request(second_ref));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        assert(!limited.request(second_ref));
        assert(native::PackagedMapSession::pixelBytes() < 160 * 1024);
        first.reset();
        limited.cancel();
        assert(read(limited, second_ref)->result.status == mt::MapTileReadStatus::Ready);
    }
    assert(native::PackagedMapSession::pixelBytes() == 0);
    // Keep the index/header intact and corrupt a pixel, so a successful catalog
    // cannot masquerade as payload integrity verification.
    {
        std::fstream file(package, std::ios::binary | std::ios::in | std::ios::out);
        std::array<uint8_t, 4096> directory_bytes{};
        file.seekg(4096);
        file.read(reinterpret_cast<char*>(directory_bytes.data()), directory_bytes.size());
        uint64_t offset = 0;
        for (size_t row = 0; row + 64 <= directory_bytes.size(); row += 64)
            if (directory_bytes[row] == 11 && !directory_bytes[row + 1])
            {
                for (unsigned byte = 0; byte < 8; ++byte) offset |= uint64_t(directory_bytes[row + 16 + byte]) << (8 * byte);
                break;
            }
        assert(offset);
        file.seekg(offset);
        char value = 0;
        file.get(value);
        file.seekp(offset);
        file.put(value ^ 1);
        file.close();
        native::PackagedMapSession corrupt(root);
        assert(prepared(corrupt).mode == native::PackagedMapMode::Tmap);
        const auto first = mt::MapTileRef{mt::MapTileLayer::Osm, 18, 131070, 131070};
        const auto rejected = read(corrupt, first);
        assert(rejected->result.status == mt::MapTileReadStatus::Invalid && !rejected->pixels);
        const auto before = corrupt.snapshot().session;
        fs::copy_file(argv[1], package, fs::copy_options::overwrite_existing);
        assert(prepared(corrupt, before).mode == native::PackagedMapMode::Tmap);
        assert(read(corrupt, first)->result.status == mt::MapTileReadStatus::Ready);
    }
    assert(native::PackagedMapSession::pixelBytes() == 0);
    fs::remove(package);
    {
        native::PackagedMapSession empty(root);
        assert(prepared(empty).mode == native::PackagedMapMode::Legacy);
        const auto empty_session = empty.snapshot().session;
        std::ofstream(package) << "corrupt";
        assert(prepared(empty, empty_session).mode == native::PackagedMapMode::Error);
        const auto before = empty.snapshot().session;
        fs::copy_file(argv[1], package, fs::copy_options::overwrite_existing);
        assert(prepared(empty, before).mode == native::PackagedMapMode::Tmap);
    }
    fs::remove_all(root); // Only the mkdtemp-owned fixture directory.
    assert(native::PackagedMapSession::pixelBytes() == 0);
    std::cout << "Native TMAP raster, alpha, annotation, media refresh and shared pixel lifetime checks passed\n";
}

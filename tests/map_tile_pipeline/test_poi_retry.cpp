#include "ui_map_runtime/map_poi/poi_tile_source.h"
#include <cassert>
#include <cstring>
#include <vector>

using namespace ui::map_tiles;
using namespace ui::map_poi;
struct Files : IMapTileFileSystem
{
    MapTileReadStatus manifest_status = MapTileReadStatus::Error;
    MapTileReadStatus index_status = MapTileReadStatus::Missing;
    mutable unsigned manifest_reads = 0, index_reads = 0;
    bool exists(const char*) const override { return true; }
    bool isDirectory(const char*) const override { return true; }
    MapTileReadResult readFile(const char* path, uint8_t* buffer, std::size_t) const override
    {
        const bool manifest = std::strstr(path, "manifest.json") != nullptr;
        if (manifest) ++manifest_reads;
        else ++index_reads;
        const auto status = manifest ? manifest_status : index_status;
        buffer[0] = 'x';
        return {status, status == MapTileReadStatus::Ready ? 1U : 0U, status == MapTileReadStatus::Ready ? 0 : -6};
    }
};
struct TestParser : Parser
{
    bool manifest(const char*, std::size_t, Policy& policy) const override
    {
        policy.enabled_levels = 1U << 16;
        return true;
    }
    bool record(const char*, std::size_t, Record&) const override { return true; }
};
int main()
{
    std::vector<uint64_t> scratch(192 * 1024 / sizeof(uint64_t));
    auto* bytes = reinterpret_cast<uint8_t*>(scratch.data());
    const MapTileRef tile{MapTileLayer::Poi, 16, 1, 2};
    TestParser parser;
    for (auto transient : {MapTileReadStatus::Error, MapTileReadStatus::RetryLater})
    {
        Files files;
        files.manifest_status = transient;
        PoiTileSource source(files, parser);
        assert(source.read(tile, bytes, scratch.size() * 8).status == transient);
        assert(files.manifest_reads == 1 && files.index_reads == 0);
        files.manifest_status = MapTileReadStatus::Ready;
        auto recovered = source.read(tile, bytes, scratch.size() * 8);
        assert(recovered.status == MapTileReadStatus::Ready && validPayload(bytes, recovered.size));
        assert(reinterpret_cast<TileHeader*>(bytes)->manifest_valid);
        assert(files.manifest_reads == 2 && files.index_reads == 1);
        files.index_status = transient;
        assert(source.read(tile, bytes, scratch.size() * 8).status == transient);
        files.index_status = MapTileReadStatus::Ready;
        recovered = source.read(tile, bytes, scratch.size() * 8);
        assert(recovered.status == MapTileReadStatus::Ready && reinterpret_cast<TileHeader*>(bytes)->count == 1);
        assert(files.manifest_reads == 2); // recovered policy stays cached
    }
    Files absent;
    absent.manifest_status = MapTileReadStatus::Missing;
    PoiTileSource source(absent, parser);
    assert(source.read(tile, bytes, scratch.size() * 8).status == MapTileReadStatus::Ready);
    assert(!reinterpret_cast<TileHeader*>(bytes)->manifest_valid);
    assert(source.read(tile, bytes, scratch.size() * 8).status == MapTileReadStatus::Ready);
    assert(absent.manifest_reads == 1 && absent.index_reads == 0);
}

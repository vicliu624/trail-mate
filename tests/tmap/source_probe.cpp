#include "ui_map_runtime/map_poi/poi_types.h"
#include "ui_map_runtime/map_tiles/native_pixel_buffer.h"
#include "ui_map_runtime/map_tiles/tmap_map_tile_source.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#ifdef _WIN32
#include <malloc.h>
#endif

using namespace ui::map_tiles;
void* alignedAllocate(size_t alignment, size_t bytes)
{
#ifdef _WIN32
    return _aligned_malloc(bytes, alignment);
#else
    void* output = nullptr;
    return posix_memalign(&output, std::max(alignment, sizeof(void*)), bytes) ? nullptr : output;
#endif
}
void alignedFree(void* memory)
{
#ifdef _WIN32
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}
class Storage final : public TmapStorage
{
  public:
    const char* paths[2];
    unsigned at = 0, opens = 0;
    uint32_t media = 1;
    size_t memory = 0, allocations = 0;
    FILE* file = nullptr;
    uint64_t length = 0;
    bool fail_allocation = false, busy_read = false;
    bool corrupt_header = false, corrupt_directory = false, corrupt_pixel = false;
    unsigned package_limit = 2;
    explicit Storage(char** argv) : paths{argv[1], argv[2]} {}
    void* allocate(size_t bytes, size_t alignment) override
    {
        if (fail_allocation) return nullptr;
        auto* output = alignedAllocate(alignment, bytes);
        if (output)
        {
            memory += bytes;
            ++allocations;
        }
        return output;
    }
    void release(void* output) override
    {
        alignedFree(output);
        --allocations;
    }
    uint32_t session() const override { return media; }
    tmap::Status nextPackage(char* output, size_t capacity) override
    {
        if (at == package_limit) return tmap::Status::Missing;
        if (std::strlen(paths[at % 2]) >= capacity) return tmap::Status::Invalid;
        std::strcpy(output, paths[at++ % 2]);
        return tmap::Status::Ok;
    }
    tmap::Status openPackage(const char* path) override
    {
        closePackage();
        file = std::fopen(path, "rb");
        if (!file) return tmap::Status::IoError;
#ifdef _WIN32
        _fseeki64(file, 0, SEEK_END);
        length = _ftelli64(file);
#else
        fseeko(file, 0, SEEK_END);
        length = ftello(file);
#endif
        ++opens;
        return tmap::Status::Ok;
    }
    uint64_t size() const override { return length; }
    void closePackage() override
    {
        if (file) std::fclose(file);
        file = nullptr;
        length = 0;
    }
    void resetEnumeration() override { at = 0; }
    tmap::Status readAt(uint64_t offset, uint8_t* output, size_t bytes) override
    {
        if (busy_read)
        {
            busy_read = false;
            return tmap::Status::Busy;
        }
#ifdef _WIN32
        if (_fseeki64(file, offset, SEEK_SET)) return tmap::Status::IoError;
#else
        if (fseeko(file, offset, SEEK_SET)) return tmap::Status::IoError;
#endif
        if (std::fread(output, 1, bytes, file) != bytes) return tmap::Status::IoError;
        if ((corrupt_header && offset == 0) || (corrupt_directory && offset == 4096) ||
            (corrupt_pixel && bytes == 131072)) output[0] ^= 1;
        return tmap::Status::Ok;
    }
};
int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    Storage storage(argv);
    TmapMapTileSource source(storage);
    auto* pixels = static_cast<uint8_t*>(alignedAllocate(16, 256 * 1024));
    assert(pixels);
    const MapTileRef world{MapTileLayer::Osm, 7, 105, 48};
    const MapTileRef china{MapTileLayer::Osm, 12, 3216, 1753};
    storage.fail_allocation = true;
    assert(source.read(world, pixels, 256 * 1024).status == MapTileReadStatus::Error);
    assert(storage.allocations == 0);
    storage.fail_allocation = false;
    storage.busy_read = true;
    assert(source.read(world, pixels, 256 * 1024).status == MapTileReadStatus::RetryLater);
    auto first = source.read(world, pixels, 256 * 1024);
    assert(first.status == MapTileReadStatus::Ready && first.format == MapTileFormat::Rgb565 && first.size == 131072);
    assert(tmap::Reader::crc32c(pixels, first.size) == 0x3ef54c05);
    assert(source.packageCount() == 2);
    const auto bytes = source.allocatedBytes();
    assert(bytes < 24 * 1024 && storage.allocations == 2);
    const auto opens = storage.opens;
    assert(source.read(world, pixels, 256 * 1024).status == MapTileReadStatus::Ready);
    assert(storage.opens == opens);
    auto second = source.read(china, pixels, 256 * 1024);
    assert(second.status == MapTileReadStatus::Ready && tmap::Reader::crc32c(pixels, second.size) == 0xa4e01293);
    const auto located = source.lookup(china);
    assert(located.status == MapTileStatus::Available && located.format == MapTileFormat::Rgb565 && located.size == 131072);
    auto poi = china;
    poi.layer = MapTileLayer::Poi;
    auto annotations = source.read(poi, pixels, 256 * 1024);
    const auto poi_info = source.lookup(poi);
    assert(poi_info.status == MapTileStatus::Available && poi_info.format == MapTileFormat::PoiRecords && poi_info.size == 0);
    assert(annotations.status == MapTileReadStatus::Ready && annotations.format == MapTileFormat::PoiRecords);
    assert(ui::map_poi::validPayload(pixels, annotations.size));
    const auto* header = reinterpret_cast<const ui::map_poi::TileHeader*>(pixels);
    assert(header->count == 42);
    assert(ui::map_poi::payloadRecords(pixels)[0].kind == ui::map::AnnotationKind::Place);
    assert(std::strcmp(ui::map_poi::payloadRecords(pixels)[0].name, u8"昆明市") == 0);
    ++storage.media;
    assert(source.read(world, pixels, 256 * 1024).status == MapTileReadStatus::Ready);
    assert(source.allocatedBytes() == bytes && storage.allocations == 2);
    source.reset();
    assert(storage.allocations == 0 && source.allocatedBytes() == 0 && !storage.file);
    storage.corrupt_header = true;
    assert(source.read(world, pixels, 262144).status == MapTileReadStatus::Invalid);
    source.reset();
    storage.corrupt_header = false;
    storage.corrupt_directory = true;
    assert(source.read(world, pixels, 262144).status == MapTileReadStatus::Invalid);
    source.reset();
    storage.corrupt_directory = false;
    storage.corrupt_pixel = true;
    assert(source.read(world, pixels, 262144).status == MapTileReadStatus::Invalid);
    source.reset();
    storage.corrupt_pixel = false;
    storage.package_limit = 257;
    MapTileReadResult overflow{};
    unsigned catalog_steps = 0;
    do
    {
        overflow = source.read(world, pixels, 262144);
        ++catalog_steps;
    } while (overflow.status == MapTileReadStatus::RetryLater && catalog_steps < 40);
    assert(overflow.status == MapTileReadStatus::Invalid && source.packageCount() == 256);
    assert(source.allocatedBytes() < 96 * 1024);
    source.reset();
    assert(storage.allocations == 0);
    storage.package_limit = 2;
    alignedFree(pixels);

    assert(storage.openPackage(argv[2]) == tmap::Status::Ok);
    auto workspace = std::make_unique<tmap::Workspace>();
    auto cursor = std::make_unique<tmap::SearchCursor>();
    tmap::Reader reader(*workspace);
    assert(reader.open(storage) == tmap::Status::Ok);
    auto countResult = [](void* count, const tmap::Poi&, tmap::SearchMode)
    { ++*static_cast<unsigned*>(count); return true; };
    for (auto mode : {tmap::SearchMode::Exact, tmap::SearchMode::Prefix, tmap::SearchMode::Substring})
    {
        unsigned matches = 0;
        assert(reader.beginSearch(u8"昆明市", std::strlen(u8"昆明市"), mode, *cursor) == tmap::Status::Ok);
        storage.busy_read = true;
        // Drop cached pages without losing the file binding, to force Busy at initialization.
        for (auto& page : workspace->pages) page.valid = false;
        assert(reader.searchStep(*cursor, 1, countResult, &matches) == tmap::Status::Busy);
        unsigned steps = 0;
        tmap::Status status;
        do
        {
            status = reader.searchStep(*cursor, 1, countResult, &matches);
            ++steps;
        } while (status == tmap::Status::More && steps < 2000);
        assert(status == tmap::Status::Ok && matches > 0 && steps > 1);
    }
    unsigned cancelled = 0;
    assert(reader.beginSearch(u8"昆明", std::strlen(u8"昆明"), tmap::SearchMode::Substring, *cursor) == tmap::Status::Ok);
    auto stopResult = [](void* count, const tmap::Poi&, tmap::SearchMode)
    { ++*static_cast<unsigned*>(count); return false; };
    auto status = reader.searchStep(*cursor, 64, stopResult, &cancelled);
    assert(status == tmap::Status::Cancelled && cancelled == 1);
    assert(reader.beginSearch("__absent_tmap_name__", 20, tmap::SearchMode::Exact, *cursor) == tmap::Status::Ok);
    unsigned absent = 0;
    assert(reader.searchStep(*cursor, 64, countResult, &absent) == tmap::Status::Ok && absent == 0);
    reader.close();
    storage.closePackage();

    std::atomic<size_t> used{0};
    auto* leased = NativePixelBuffer::create(131072, used, 140000, alignedAllocate, alignedFree);
    assert(leased && used > 131072);
    assert(!NativePixelBuffer::create(131072, used, 140000, alignedAllocate, alignedFree));
    NativePixelBuffer::retain(leased);
    NativePixelBuffer::release(leased);
    assert(used > 0);
    NativePixelBuffer::release(leased);
    assert(used == 0);
    auto* rgba = NativePixelBuffer::create(262144, used, 300000, alignedAllocate, alignedFree);
    assert(rgba);
    std::memset(rgba, 0, 262144);
    rgba[0] = 1;
    rgba[1] = 2;
    rgba[2] = 3;
    rgba[3] = 4;
    NativePixelBuffer::convertRgbaToBgra(rgba, 262144);
    NativePixelBuffer::convertRgbaToBgra(rgba, 262144);
    assert(rgba[0] == 3 && rgba[1] == 2 && rgba[2] == 1 && rgba[3] == 4);
    NativePixelBuffer::release(rgba);
    assert(used == 0);
    std::printf("source_probe: world/china/annotations/busy/session/ownership/CRC/capacity/search/cancel PASS; catalog_bytes=%u\n", static_cast<unsigned>(bytes));
}

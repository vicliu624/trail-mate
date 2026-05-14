#include "ui_map_runtime/map_tiles/filesystem_map_tile_source.h"

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

namespace
{

class FakeFileSystem final : public ui::map_tiles::IMapTileFileSystem
{
  public:
    std::vector<std::string> files;
    std::vector<std::string> dirs;

    bool exists(const char* path) const override
    {
        return contains(files, path);
    }

    bool isDirectory(const char* path) const override
    {
        return contains(dirs, path);
    }

    bool readFile(const char* path,
                  uint8_t* buffer,
                  std::size_t capacity,
                  std::size_t& out_size) const override
    {
        out_size = 0;
        if (!exists(path) || capacity < 3 || buffer == nullptr)
        {
            return false;
        }
        buffer[0] = 1;
        buffer[1] = 2;
        buffer[2] = 3;
        out_size = 3;
        return true;
    }

  private:
    static bool contains(const std::vector<std::string>& values, const char* path)
    {
        if (path == nullptr)
        {
            return false;
        }
        for (const auto& value : values)
        {
            if (value == path)
            {
                return true;
            }
        }
        return false;
    }
};

void test_lookup_and_read()
{
    FakeFileSystem fs;
    fs.files.push_back("/sd/maps/base/osm/4/8/6.png");

    ui::map_tiles::FilesystemMapTileSource source(fs, "/sd");
    ui::map_tiles::MapTileRef ref{};
    ref.layer = ui::map_tiles::MapTileLayer::Osm;
    ref.z = 4;
    ref.x = 8;
    ref.y = 6;

    const auto hit = source.lookup(ref);
    assert(hit.status == ui::map_tiles::MapTileStatus::Available);
    assert(hit.format == ui::map_tiles::MapTileFormat::Png);

    uint8_t buffer[4]{};
    std::size_t out_size = 0;
    ui::map_tiles::MapTileFormat format = ui::map_tiles::MapTileFormat::Unknown;
    assert(source.read(ref, buffer, sizeof(buffer), out_size, format));
    assert(out_size == 3);
    assert(format == ui::map_tiles::MapTileFormat::Png);
    assert(buffer[0] == 1 && buffer[1] == 2 && buffer[2] == 3);

    ref.y = 7;
    const auto miss = source.lookup(ref);
    assert(miss.status == ui::map_tiles::MapTileStatus::Missing);
}

void test_directories()
{
    FakeFileSystem fs;
    fs.dirs.push_back("A:/maps/base/terrain");
    fs.dirs.push_back("A:/maps/contour/major-100");

    ui::map_tiles::FilesystemMapTileSource source(fs, "A:");
    assert(source.layerDirectoryAvailable(ui::map_tiles::MapTileLayer::Terrain));
    assert(!source.layerDirectoryAvailable(ui::map_tiles::MapTileLayer::Satellite));
    assert(source.anyContourDirectoryAvailable());
}

void test_resolve_path()
{
    FakeFileSystem fs;
    ui::map_tiles::FilesystemMapTileSource source(fs, "A:");

    ui::map_tiles::MapTileRef ref{};
    ref.layer = ui::map_tiles::MapTileLayer::Satellite;
    ref.z = 9;
    ref.x = 82;
    ref.y = 190;

    char path[160]{};
    assert(source.resolvePath(ref, path, sizeof(path)));
    assert(std::strcmp(path, "A:/maps/base/satellite/9/82/190.jpg") == 0);
}

} // namespace

int main()
{
    test_lookup_and_read();
    test_directories();
    test_resolve_path();
    return 0;
}

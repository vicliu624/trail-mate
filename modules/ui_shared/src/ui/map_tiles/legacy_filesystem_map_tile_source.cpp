#include "ui/map_tiles/legacy_filesystem_map_tile_source.h"

namespace ui
{
namespace map_tiles
{

LegacyFilesystemMapTileSource::LegacyFilesystemMapTileSource(
    IMapTileFileSystem& file_system,
    const char* root_prefix)
    : file_system_(file_system),
      resolver_(root_prefix)
{
}

MapTileLookupResult LegacyFilesystemMapTileSource::lookup(const MapTileRef& ref) const
{
    MapTileLookupResult result{};
    result.format = mapTileFormatForLayer(ref.layer);

    char path[160]{};
    if (!resolver_.resolvePath(ref, path, sizeof(path)))
    {
        result.status = MapTileStatus::Error;
        return result;
    }

    result.status = file_system_.exists(path) ? MapTileStatus::Available
                                              : MapTileStatus::Missing;
    return result;
}

bool LegacyFilesystemMapTileSource::read(const MapTileRef& ref,
                                         uint8_t* buffer,
                                         std::size_t capacity,
                                         std::size_t& out_size,
                                         MapTileFormat& out_format) const
{
    out_size = 0;
    out_format = mapTileFormatForLayer(ref.layer);

    char path[160]{};
    if (!resolver_.resolvePath(ref, path, sizeof(path)))
    {
        out_format = MapTileFormat::Unknown;
        return false;
    }

    return file_system_.readFile(path, buffer, capacity, out_size);
}

bool LegacyFilesystemMapTileSource::resolvePath(const MapTileRef& ref,
                                                char* out_path,
                                                std::size_t out_size) const
{
    return resolver_.resolvePath(ref, out_path, out_size);
}

bool LegacyFilesystemMapTileSource::resolveDirectory(MapTileLayer layer,
                                                     char* out_path,
                                                     std::size_t out_size) const
{
    return resolver_.resolveDirectory(layer, out_path, out_size);
}

bool LegacyFilesystemMapTileSource::layerDirectoryAvailable(MapTileLayer layer) const
{
    char path[160]{};
    return resolver_.resolveDirectory(layer, path, sizeof(path)) &&
           file_system_.isDirectory(path);
}

bool LegacyFilesystemMapTileSource::anyContourDirectoryAvailable() const
{
    static const MapTileLayer kContourLayers[] = {
        MapTileLayer::ContourMajor500,
        MapTileLayer::ContourMajor200,
        MapTileLayer::ContourMajor100,
        MapTileLayer::ContourMajor50,
        MapTileLayer::ContourMajor25,
    };

    for (MapTileLayer layer : kContourLayers)
    {
        if (layerDirectoryAvailable(layer))
        {
            return true;
        }
    }
    return false;
}

} // namespace map_tiles
} // namespace ui

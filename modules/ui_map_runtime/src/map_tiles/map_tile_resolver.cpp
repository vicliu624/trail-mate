#include "ui_map_runtime/map_tiles/map_tile_resolver.h"

#include <cstdio>
#include <cstring>

namespace ui
{
namespace map_tiles
{
namespace
{

const char* baseLayerDir(MapTileLayer layer)
{
    switch (layer)
    {
    case MapTileLayer::Terrain:
        return "terrain";
    case MapTileLayer::Satellite:
        return "satellite";
    case MapTileLayer::Osm:
    default:
        return "osm";
    }
}

const char* contourLayerDir(MapTileLayer layer)
{
    switch (layer)
    {
    case MapTileLayer::ContourMajor500:
        return "major-500";
    case MapTileLayer::ContourMajor200:
        return "major-200";
    case MapTileLayer::ContourMajor100:
        return "major-100";
    case MapTileLayer::ContourMajor50:
        return "major-50";
    case MapTileLayer::ContourMajor25:
        return "major-25";
    default:
        return nullptr;
    }
}

const char* extensionFor(MapTileLayer layer)
{
    return mapTileFormatForLayer(layer) == MapTileFormat::Jpeg ? "jpg" : "png";
}

bool writePath(char* out_path,
               std::size_t out_size,
               const char* root,
               const char* category,
               const char* layer,
               uint8_t z,
               uint32_t x,
               uint32_t y,
               const char* extension)
{
    if (!out_path || out_size == 0 || !category || !layer || !extension)
    {
        return false;
    }

    const bool has_root = root && root[0] != '\0';
    const char* separator = "";
    if (has_root)
    {
        const std::size_t root_len = std::strlen(root);
        separator = root[root_len - 1] == '/' ? "" : "/";
    }

    const int written = std::snprintf(out_path,
                                      out_size,
                                      "%s%s%s/%s/%u/%lu/%lu.%s",
                                      has_root ? root : "",
                                      separator,
                                      category,
                                      layer,
                                      static_cast<unsigned>(z),
                                      static_cast<unsigned long>(x),
                                      static_cast<unsigned long>(y),
                                      extension);
    if (written < 0 || static_cast<std::size_t>(written) >= out_size)
    {
        out_path[0] = '\0';
        return false;
    }
    return true;
}

bool writeDirectory(char* out_path,
                    std::size_t out_size,
                    const char* root,
                    const char* category,
                    const char* layer)
{
    if (!out_path || out_size == 0 || !category || !layer)
    {
        return false;
    }

    const bool has_root = root && root[0] != '\0';
    const char* separator = "";
    if (has_root)
    {
        const std::size_t root_len = std::strlen(root);
        separator = root[root_len - 1] == '/' ? "" : "/";
    }

    const int written = std::snprintf(out_path,
                                      out_size,
                                      "%s%s%s/%s",
                                      has_root ? root : "",
                                      separator,
                                      category,
                                      layer);
    if (written < 0 || static_cast<std::size_t>(written) >= out_size)
    {
        out_path[0] = '\0';
        return false;
    }
    return true;
}

} // namespace

MapTileLayer mapTileLayerFromBaseSource(uint8_t map_source)
{
    switch (map_source)
    {
    case 1:
        return MapTileLayer::Terrain;
    case 2:
        return MapTileLayer::Satellite;
    case 0:
    default:
        return MapTileLayer::Osm;
    }
}

MapTileLayer mapTileContourLayerForZoom(int zoom, bool* out_supported)
{
    if (out_supported)
    {
        *out_supported = zoom > 7;
    }

    if (zoom <= 7)
    {
        return MapTileLayer::ContourMajor100;
    }
    if (zoom == 8)
    {
        return MapTileLayer::ContourMajor500;
    }
    if (zoom == 9)
    {
        return MapTileLayer::ContourMajor200;
    }
    if (zoom == 10)
    {
        return MapTileLayer::ContourMajor500;
    }
    if (zoom == 11)
    {
        return MapTileLayer::ContourMajor200;
    }
    if (zoom <= 14)
    {
        return MapTileLayer::ContourMajor100;
    }
    if (zoom <= 16)
    {
        return MapTileLayer::ContourMajor50;
    }
    return MapTileLayer::ContourMajor25;
}

MapTileFormat mapTileFormatForLayer(MapTileLayer layer)
{
    return layer == MapTileLayer::Satellite ? MapTileFormat::Jpeg : MapTileFormat::Png;
}

bool mapTileLayerIsContour(MapTileLayer layer)
{
    return contourLayerDir(layer) != nullptr;
}

MapTileResolver::MapTileResolver()
{
    setRootPrefix("");
}

MapTileResolver::MapTileResolver(const char* root_prefix)
{
    setRootPrefix(root_prefix);
}

void MapTileResolver::setRootPrefix(const char* root_prefix)
{
    if (!root_prefix)
    {
        root_prefix_[0] = '\0';
        return;
    }

    std::snprintf(root_prefix_, sizeof(root_prefix_), "%s", root_prefix);
    root_prefix_[sizeof(root_prefix_) - 1] = '\0';
}

const char* MapTileResolver::rootPrefix() const
{
    return root_prefix_;
}

bool MapTileResolver::resolvePath(const MapTileRef& ref,
                                  char* out_path,
                                  std::size_t out_size) const
{
    if (mapTileLayerIsContour(ref.layer))
    {
        return writePath(out_path,
                         out_size,
                         root_prefix_,
                         "maps/contour",
                         contourLayerDir(ref.layer),
                         ref.z,
                         ref.x,
                         ref.y,
                         extensionFor(ref.layer));
    }

    return writePath(out_path,
                     out_size,
                     root_prefix_,
                     "maps/base",
                     baseLayerDir(ref.layer),
                     ref.z,
                     ref.x,
                     ref.y,
                     extensionFor(ref.layer));
}

bool MapTileResolver::resolveDirectory(MapTileLayer layer,
                                       char* out_path,
                                       std::size_t out_size) const
{
    if (mapTileLayerIsContour(layer))
    {
        return writeDirectory(out_path,
                              out_size,
                              root_prefix_,
                              "maps/contour",
                              contourLayerDir(layer));
    }

    return writeDirectory(out_path,
                          out_size,
                          root_prefix_,
                          "maps/base",
                          baseLayerDir(layer));
}

} // namespace map_tiles
} // namespace ui

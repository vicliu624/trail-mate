#pragma once

#include <cstddef>
#include <cstdint>

namespace ui
{
namespace map_tiles
{

enum class MapTileLayer : uint8_t
{
    Osm,
    Terrain,
    Satellite,
    ContourMajor500,
    ContourMajor200,
    ContourMajor100,
    ContourMajor50,
    ContourMajor25,
};

enum class MapTileFormat : uint8_t
{
    Unknown,
    Png,
    Jpeg,
};

enum class MapTileStatus : uint8_t
{
    Unknown,
    Available,
    Missing,
    Error,
};

struct MapTileRef
{
    MapTileLayer layer = MapTileLayer::Osm;
    uint8_t z = 0;
    uint32_t x = 0;
    uint32_t y = 0;
};

struct MapTilePayload
{
    MapTileRef ref;
    MapTileFormat format = MapTileFormat::Unknown;
    const uint8_t* data = nullptr;
    std::size_t size = 0;
};

struct MapTileLookupResult
{
    MapTileStatus status = MapTileStatus::Unknown;
    MapTileFormat format = MapTileFormat::Unknown;
    std::size_t size = 0;
};

MapTileLayer mapTileLayerFromBaseSource(uint8_t map_source);
MapTileLayer mapTileContourLayerForZoom(int zoom, bool* out_supported = nullptr);
MapTileFormat mapTileFormatForLayer(MapTileLayer layer);
bool mapTileLayerIsContour(MapTileLayer layer);

} // namespace map_tiles
} // namespace ui

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

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
    ContourMinor100,
    ContourMinor50,
    ContourMinor20,
    ContourMinor10,
    ContourMinor5,
    Poi,
};

enum class MapTileFormat : uint8_t
{
    Unknown,
    Png,
    Jsonl,
    PoiRecords,
    Rgb565,
    Rgba8888,
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

/**
 * Format the stable XYZ identity shown while a tile is loading or missing.
 *
 * This is shared by embedded and Linux renderers so a tile has the same
 * diagnostic identity on every target.
 */
inline void formatMapTileCoordinateLabel(std::uint8_t z,
                                         std::uint32_t x,
                                         std::uint32_t y,
                                         char* out,
                                         std::size_t out_size) noexcept
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }
    std::snprintf(out,
                  out_size,
                  "z=%u\nx=%u\ny=%u",
                  static_cast<unsigned>(z),
                  static_cast<unsigned>(x),
                  static_cast<unsigned>(y));
}

MapTileLayer mapTileLayerFromBaseSource(uint8_t map_source);
MapTileLayer mapTileContourLayerForZoom(int zoom, bool* out_supported = nullptr);
MapTileFormat mapTileFormatForLayer(MapTileLayer layer);
bool mapTileLayerIsContour(MapTileLayer layer);

} // namespace map_tiles
} // namespace ui
